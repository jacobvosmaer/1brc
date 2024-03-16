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

struct record {
  char name[128];
  int namesize, num;
  int64_t total, min, max;
};

int namelen(const struct record *r) { return r->namesize; }
const char *nameof(const struct record *r) { return r->name; }

struct threaddata {
  struct record records[1 << EXP];
  int nrecords;
  char *start, *end;
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
  struct record *r;

  while (1) {
    i = ht_lookup(hash, EXP, i);
    r = t->records + i;
    if (!r->namesize) {
      assert(t->nrecords < nelem(t->records));
      t->nrecords++;
      assert(size + 1 <= nelem(r->name));
      r->namesize = size;
      memmove(r->name, name, size);
      r->name[size] = 0;
      return r;
    } else if (size == namelen(r) && !memcmp(name, nameof(r), size)) {
      return r;
    }
  }
}

void printrecords(struct threaddata *t) {
  struct record *r;
  for (r = t->records; r < t->records + t->nrecords; r++)
    if (r->namesize)
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

int64_t parsenum(char **pp, char *end) {
  int64_t val, sign, x, x10, x100;
  int period;
  char *p = *pp;

  if (end - p < 8) {
    sign = 1 - 2 * (*p == '-');
    p += (*p == '-');
    for (val = 0; *p && *p != '\n'; p++)
      if (*p != '.')
        val = 10 * val + digit(*p);
    val *= sign;
    *pp = p;
    return val;
  }

  x = *(int64_t *)p;
  /* Test if first character has bit 4 set to 0 */
  sign = (x << 59) >> 63; /* 0 if negative, -1 if positive */
  x &= ~0xff | sign;      /* mask minus if present */
  period = __builtin_ctz(~x & 0x10101000);
  /* mask period and all characters 1 or more positions before it */
  x &= ~(0xffffffffffff00ffL << (period - 4));

  x <<= (period == 12) * 8; /* add leading zero for case "1.2" */
  x >>= (period == 28) * 8; /* remove leading zero left by minus for "-12.3" */
  x &= 0x0f000f0f;          /* ascii to int */

  x10 = 10 * (x << 16);
  x100 = 100 * (x << 24);
  val = (x >> 24) & 0xff;
  val += (x10 >> 24) & 0xff;
  val += (x100 >> 24) & 0xff;
  val *= -1 - 2 * sign;
  *pp += (period + 12) / 8;
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
             {"12.3\n", 123, 4},         {"-12.3\n", -123, 5},
             {"1.2\n", 12, 3},           {"-1.2\n", -12, 4},
             {"12.3\naaaaaaaa", 123, 4}, {"-12.3\naaaaaaaa", -123, 5},
             {"1.2\naaaaaaaa", 12, 3},   {"-1.2\naaaaaaaa", -12, 4},
         };
  for (t = tests; t < endof(tests); t++) {
    char *p = t->in;
    int f = 0;
    int actual = parsenum(&p, p + strlen(p)), off = p - t->in;
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
  char *line;
  struct threaddata *t = data;

  for (line = t->start; line < t->end;) {
    char *p = line;
    int64_t val;
    uint64_t hash = 0;
    while (*p != ';')
      hashupdate(&hash, *p++);
    r = upsert(t, line, p - line, hash);
    p++;

    val = parsenum(&p, t->end);
    updaterecord(r, val, 1, val, val);
    if (*p != '\n')
      errx(-1, "missing newline");
    line = p + 1; /* consume newline */
  }
  return 0;
}

int main(int argc, char **argv) {
  struct record *r;
  struct stat st;
  char *in;
  struct threaddata *t, *t0 = threaddata;
  int i;

  if (argc == 2 && !strcmp("-test", argv[1])) {
    testparsenum();
    testupsert();
    return 0;
  } else if (argc != 1) {
    errx(-1, "Usage: c8 [-test]");
  }

  if (fstat(0, &st))
    err(-1, "fstat stdin");
  if (!(in = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, 0, 0)))
    err(-1, "mmap stdin");

  for (i = 0; i < nelem(threaddata); i++) {
    t = threaddata + i;
    t->start = i == 0 ? in : (t - 1)->end;
    if (i == nelem(threaddata) - 1) {
      t->end = in + st.st_size;
    } else {
      assert(t->end = memchr(in + (i + 1) * (st.st_size / nelem(threaddata)),
                             '\n', 256));
      t->end++;
    }
    assert(!pthread_create(&t->thread, 0, processinput, t));
  }

  for (t = threaddata; t < threaddata + nelem(threaddata); t++) {
    assert(!pthread_join(t->thread, 0));
    if (t > t0) {
      for (r = t->records; r < t->records + t->nrecords; r++)
        if (r->namesize)
          updaterecord(upsertsz(t0, nameof(r), namelen(r)), r->total, r->num,
                       r->min, r->max);
    }
  }

  /* This qsort will invalidate recordindex but that is OK because we don't need
   * recordindex anymore. */
  qsort(t0->records, t0->nrecords, sizeof(*t0->records), recordnameasc);

  for (r = t0->records, i = 0; r < t0->records + t0->nrecords; r++)
    if (r->namesize)
      printf("%s%s=%.1f/%.1f/%.1f", !i++ ? "{" : ", ", nameof(r),
             (double)r->min / 10.0, (double)r->total / (10.0 * (double)r->num),
             (double)r->max / 10.0);
  puts("}");

  return 0;
}
