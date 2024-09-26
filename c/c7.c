#include <err.h>
#include <pthread.h>
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

#ifndef EXP
#define EXP 16
#endif
#ifndef NTHREAD
#define NTHREAD 16
#endif

struct record {
  char *name;
  int64_t total;
  int16_t min, max;
  int32_t num;
};

struct threaddata {
  struct record records[1 << 14], *recordindex[1 << EXP];
  int nrecords;
  char *start, *end;
  pthread_t thread;
} threaddata[NTHREAD];

int recordnameasc(const void *a_, const void *b_) {
  const struct record *a = a_, *b = b_;
  return strcmp(a->name, b->name);
}

/* From https://nullprogram.com/blog/2022/08/08/ */
int ht_lookup(uint64_t hash, int exp, int idx) {
  uint32_t mask = ((uint32_t)1 << exp) - 1;
  uint32_t step = (hash >> (64 - exp)) | 1;
  return (idx + step) & mask;
}

uint64_t hashinit(void) { return 3141592653; }
void hashupdate(uint64_t *h, char c) { *h = 111 * *h + (uint64_t)c; }
uint64_t hashstr(char *s) {
  uint64_t h = hashinit();
  while (*s)
    hashupdate(&h, *s++);
  return h;
}

int equalmemstr(char *mem, int size, char *str) {
  assert(size >= 0);
  while (size && *str && *mem == *str) {
    mem++;
    str++;
    size--;
  }
  return !size && !*str;
}

struct record *upsert(struct threaddata *t, char *name, int size,
                      uint64_t hash) {
  int i = hash;
  struct record **rp;

  while (1) {
    i = ht_lookup(hash, EXP, i);
    rp = t->recordindex + i;
    if (!*rp) {
      assert(t->nrecords < nelem(t->records));
      *rp = t->records + t->nrecords++;
      assert((*rp)->name = malloc(size + 1));
      memmove((*rp)->name, name, size);
      (*rp)->name[size] = 0;
      return *rp;
    } else if (equalmemstr(name, size, (*rp)->name)) {
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

struct record *upsertstr(struct threaddata *t, char *s) {
  return upsert(t, s, strlen(s), hashstr(s));
}

void testupsert(void) {
  struct record *r, *abc, *def;
  struct threaddata *t = threaddata;

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

  for (r = t->records; r < t->records + nelem(t->records); r++)
    free(r->name);
  t->nrecords = 0;
  memset(t->recordindex, 0, sizeof(t->recordindex));
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

void *processinput(void *data) {
  struct record *r;
  char *line;
  struct threaddata *t = data;

  for (line = t->start; line < t->end;) {
    char *p = line;
    int64_t val, sign;
    uint64_t hash = hashinit();
    while (*p != ';')
      hashupdate(&hash, *p++);
    r = upsert(t, line, p - line, hash);
    p++;
    sign = 1;
    if (*p == '-') {
      sign = -1;
      p++;
    }
    for (val = 0; *p && *p != '\n'; p++)
      if (*p != '.')
        val = 10 * val + digit(*p);
    val *= sign;
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
  struct threaddata *t;
  int i;

  if (argc == 2 && !strcmp("-test", argv[1])) {
    printf("sizeof(struct record)=%ld\n", sizeof(struct record));
    testupsert();
    return 0;
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
    if (t > threaddata)
      for (r = t->records; r < t->records + t->nrecords; r++)
        updaterecord(upsertstr(threaddata, r->name), r->total, r->num, r->min,
                     r->max);
  }

  /* This qsort will invalidate recordindex but that is OK because we don't need
   * recordindex anymore. */
  qsort(threaddata->records, threaddata->nrecords, sizeof(*threaddata->records),
        recordnameasc);

  for (r = threaddata->records; r < threaddata->records + threaddata->nrecords;
       r++)
    printf("%s%s=%.1f/%.1f/%.1f", r == threaddata->records ? "{" : ", ",
           r->name, (double)r->min / 10.0,
           (double)r->total / (10.0 * (double)r->num), (double)r->max / 10.0);
  puts("}");

  return 0;
}
