#include <err.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define assert(x)                                                              \
  if (!(x))                                                                    \
  __builtin_trap()
#define nelem(x) (sizeof(x) / sizeof(*(x)))
#define endof(x) ((x) + nelem(x))

#ifndef EXP
#define EXP 16
#endif
#ifndef NTHREAD
#define NTHREAD 16
#endif

#define CHUNKSIZE (2 << 20)

struct record {
  char name[128];
  int namesize, num;
  int64_t total, min, max;
};

int namelen(const struct record *r) { return r->namesize; }
const char *nameof(const struct record *r) { return r->name; }

struct threaddata {
  struct record records[1 << 14], *recordindex[1 << EXP];
  int nrecords;
  char *start, *end, **nextchunk;
  pthread_t thread;
} threaddata[NTHREAD];

int recordnameasc(const void *a_, const void *b_) {
  const struct record *a = a_, *b = b_;
  int alen = namelen(a), blen = namelen(b);
  int cmp = memcmp(nameof(a), nameof(b), alen < blen ? alen : blen);
  return cmp + !cmp * (alen < blen ? -1 : 1);
}

/* From https://nullprogram.com/blog/2022/08/08/ */
int ht_lookup(uint64_t hash, int exp, int idx) {
  uint32_t mask = ((uint32_t)1 << exp) - 1;
  uint32_t step = (hash >> (64 - exp)) | 1;
  return (idx + step) & mask;
}

void hashupdate(uint64_t *h, char c) { *h = 111 * *h + (uint64_t)c; }
uint64_t hashstr(char *s) {
  uint64_t h = 0;
  while (*s)
    hashupdate(&h, *s++);
  return h;
}

struct record *upsert(struct threaddata *t, const char *name, int size,
                      uint64_t hash) {
  int i = hash;
  struct record **rp;

  while (1) {
    i = ht_lookup(hash, EXP, i);
    rp = t->recordindex + i;
    if (!*rp) {
      assert(t->nrecords < nelem(t->records));
      *rp = t->records + t->nrecords++;
      assert(size + 1 <= nelem((*rp)->name));
      (*rp)->namesize = size;
      memmove((*rp)->name, name, size);
      (*rp)->name[size] = 0;
      return *rp;
    } else if (size == namelen(*rp) && !memcmp(name, nameof(*rp), size)) {
      return *rp;
    }
  }
}

void printrecords(struct threaddata *t) {
  struct record *r;
  for (r = t->records; r < t->records + t->nrecords; r++)
    printf("%s\n", r->name);
  putchar('\n');
}

struct record *upsertsz(struct threaddata *t, const char *s, int size) {
  uint64_t h = 0;
  int i;
  for (i = 0; i < size; i++)
    hashupdate(&h, s[i]);
  return upsert(t, s, size, h);
}

struct record *upsertstr(struct threaddata *t, char *s) {
  return upsertsz(t, s, strlen(s));
}

void testupsert(void) {
  struct record *abc, *def;
  struct threaddata *t;
  assert(t = calloc(sizeof(*t), 1));

  abc = upsertstr(t, "abc");
  assert(t->nrecords == 1);
  printrecords(t);
  def = upsertstr(t, "def");
  assert(t->nrecords == 2);
  printrecords(t);
  assert(upsertstr(t, "abc") == abc);
  assert(t->nrecords == 2);
  printrecords(t);
  assert(upsertstr(t, "def") == def);
  assert(t->nrecords == 2);
  printrecords(t);
  upsertstr(t, "012");
  assert(t->nrecords == 3);
  printrecords(t);

  free(t);
}

int digit(char c) {
  assert(c >= '0' && c <= '9');
  return c - '0';
}

void updaterecord(struct record *r, int64_t total, int num, int64_t min,
                  int64_t max) {
  if (!r->num || min < r->min)
    r->min = min;
  if (!r->num || max > r->max)
    r->max = max;
  r->total += total;
  r->num += num;
}

int64_t parsenum(char **pp) {
  int64_t val, sign;
  char *p = *pp;

  sign = 1 - 2 * (*p == '-');
  p += (*p == '-');
  for (val = 0; *p && *p != '\n'; p++)
    if (*p != '.')
      val = 10 * val + digit(*p);
  val *= sign;
  *pp = p;
  return val;
}

void failf(int *failcount, char *fmt, ...) {
  va_list ap;
  fprintf(stderr, "fail: ");
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
  *failcount += 1;
}

void testparsenum(void) {
  int failed = 0;
  struct {
    char *in;
    int out, off;
  } * t, tests[] = {
             {"12.3\n", 123, 4},
             {"-12.3\n", -123, 5},
             {"1.2\n", 12, 3},
             {"-1.2\n", -12, 4},
         };
  for (t = tests; t < endof(tests); t++) {
    char *p = t->in;
    int f = 0;
    int actual = parsenum(&p), off = p - t->in;
    warnx("t->in=%s", t->in);
    if (t->out != actual)
      failf(&f, "expected %d, got %d", t->out, actual);
    if (t->off != off)
      failf(&f, "expected pointer advanced by %d, got %d", t->off, off);
    if (t->in[off] != '\n')
      failf(&f, "expected to point to newline, got %02x", t->in[off]);
    failed += !!f;
  }
  if (failed)
    warnx("testparsenum: %d/%ld tests failed", failed, t - tests);
  else
    warnx("testparsenum: %ld tests ok", t - tests);
}

void *processinput(void *data) {
  struct record *r;
  char *line, *chunk;
  struct threaddata *t = data;

  for (;;) {
    chunk = __atomic_add_fetch(t->nextchunk, CHUNKSIZE, __ATOMIC_RELAXED) -
            CHUNKSIZE;
    if (chunk >= t->end)
      break;
    if (chunk > t->start) {
      while (*chunk != '\n')
        chunk++;
      chunk++;
    }

    for (line = chunk; line < chunk + CHUNKSIZE && line < t->end;) {
      char *p = line;
      int64_t val;
      uint64_t hash = 0;
      while (*p != ';')
        hashupdate(&hash, *p++);
      r = upsert(t, line, p - line, hash);
      p++;

      val = parsenum(&p);
      updaterecord(r, val, 1, val, val);
      if (*p != '\n')
        errx(-1, "missing newline");
      line = p + 1; /* consume newline */
    }
  }

  return 0;
}

int main(int argc, char **argv) {
  struct record *r;
  struct stat st;
  char *in, *chunk;
  struct threaddata *t, *t0 = threaddata;
  int i;

  if (argc == 2 && !strcmp("-test", argv[1])) {
    testparsenum();
    testupsert();
    return 0;
  } else if (argc != 1) {
    errx(-1, "Usage: c11 [-test]");
  }

  if (fstat(0, &st))
    err(-1, "fstat stdin");
  if (!(in = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, 0, 0)))
    err(-1, "mmap stdin");

  chunk = in;
  for (i = 0; i < nelem(threaddata); i++) {
    t = threaddata + i;
    t->start = in;
    t->end = in + st.st_size;
    t->nextchunk = &chunk;
    assert(!pthread_create(&t->thread, 0, processinput, t));
  }

  for (t = threaddata; t < threaddata + nelem(threaddata); t++) {
    assert(!pthread_join(t->thread, 0));
    if (t > t0) {
      for (r = t->records; r < t->records + t->nrecords; r++)
        updaterecord(upsertsz(t0, nameof(r), namelen(r)), r->total, r->num,
                     r->min, r->max);
    }
  }

  /* This qsort will invalidate recordindex but that is OK because we don't need
   * recordindex anymore. */
  qsort(t0->records, t0->nrecords, sizeof(*t0->records), recordnameasc);

  for (r = t0->records; r < t0->records + t0->nrecords; r++)
    printf("%s%s=%.1f/%.1f/%.1f", r == t0->records ? "{" : ", ", nameof(r),
           (double)r->min / 10.0, (double)r->total / (10.0 * (double)r->num),
           (double)r->max / 10.0);
  puts("}");

  return 0;
}
