#include <err.h>
#include <pthread.h>
#include <stdatomic.h>
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
#define EXP 14
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
  struct record records[1 << EXP];
  int nrecords;
  struct names {
    char data[1 << 18], *end;
  } names;
  char *start, *end;
  pthread_t thread;
  atomic_ptrdiff_t *offset;
} threaddata[NTHREAD];

char *namealloc(struct threaddata *t, int size) {
  enum { align = 8 };
  char *ret;
  struct names *names = &t->names;
  assert(size >= 0);
  size = align * (size / align) + align * ((size & (align - 1)) > 0);
  if (!names->end)
    names->end = names->data;
  assert(endof(names->data) - names->end >= size);
  ret = names->end;
  names->end += size;
  return ret;
}

int recordnameasc(const void *a_, const void *b_) {
  const struct record *a = a_, *b = b_;
  if (!a->name && !b->name)
    return 0;
  else if (!a->name)
    return 1;
  else if (!b->name)
    return -1;
  else
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
  int i = ht_lookup(hash, EXP, hash);
  struct record *rp;

  while (1) {
    rp = t->records + i;
    if (!rp->name) {
      t->nrecords++;
      rp->name = namealloc(t, size + 1);
      memmove(rp->name, name, size);
      rp->name[size] = 0;
      return rp;
    } else if (equalmemstr(name, size, rp->name)) {
      return rp;
    } else {
      i = (i + 1) & (((uint32_t)1 << EXP) - 1);
    }
  }
}

void printrecords(struct threaddata *t) {
  struct record *r;
  for (r = t->records; r < endof(t->records); r++)
    if (r->name)
      printf("%s\n", r->name);
  putchar('\n');
}

struct record *upsertstr(struct threaddata *t, char *s) {
  return upsert(t, s, strlen(s), hashstr(s));
}

void testupsert(void) {
  struct record *abc, *def;
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

#define CHUNKSIZE (1 << 20)

void *processinput(void *data) {
  struct record *r;
  char *line, *chunk, *chunkend;
  struct threaddata *t = data;
  while (chunk = t->start + atomic_fetch_add(t->offset, CHUNKSIZE),
         chunk < t->end) {
    chunkend = chunk + CHUNKSIZE > t->end ? t->end : chunk + CHUNKSIZE;
    if (chunk > t->start) {
      chunk = memchr(chunk, '\n', chunkend - chunk);
      assert(chunk);
      chunk++;
    }

    for (line = chunk; line < chunkend;) {
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
  }
  return 0;
}

int main(int argc, char **argv) {
  struct record *r;
  struct stat st;
  char *in;
  struct threaddata *t;
  int i, singlethreaded = argc == 2 && !strcmp("-singlethreaded", argv[1]);
  atomic_ptrdiff_t inputoffset = 0;

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
    t->start = in;
    t->end = in + st.st_size;
    t->offset = &inputoffset;
    if (!singlethreaded)
      assert(!pthread_create(&t->thread, 0, processinput, t));
  }

  if (singlethreaded) {
    fprintf(stderr, "processing input on main thread\n");
    processinput(threaddata);
  } else {
    for (t = threaddata; t < threaddata + nelem(threaddata); t++) {
      assert(!pthread_join(t->thread, 0));
      if (t > threaddata)
        for (r = t->records; r < endof(t->records); r++)
          if (r->name)
            updaterecord(upsertstr(threaddata, r->name), r->total, r->num,
                         r->min, r->max);
    }
  }

  /* This qsort will invalidate recordindex but that is OK because we don't need
   * recordindex anymore. */
  qsort(threaddata->records, nelem(threaddata->records),
        sizeof(*threaddata->records), recordnameasc);

  for (r = threaddata->records; r < endof(threaddata->records) && r->name; r++)
    printf("%s%s=%.1f/%.1f/%.1f", r == threaddata->records ? "{" : ", ",
           r->name, (double)r->min / 10.0,
           (double)r->total / (10.0 * (double)r->num), (double)r->max / 10.0);
  puts("}");

  return 0;
}
