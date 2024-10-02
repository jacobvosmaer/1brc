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

uint64_t packsigned(int64_t x, int bits, int shift) {
  uint64_t out = x < 0 ? x + (1LL << bits) : x;
  return out << shift;
}

int64_t unpacksigned(uint64_t packed, int bits, int shift) {
  int64_t out = ((packed >> shift) & ((1ULL << bits) - 1));
  if (out >= (1LL << (bits - 1)))
    out -= (1LL << bits);
  return out;
}

void testpacksigned(void) {
  int bits = 8, fail = 0, i, shift;

  for (shift = 0; shift < 2; shift++)
    for (i = -128; i < 128; i++) {
      uint64_t packed = packsigned(i, bits, shift);
      int64_t unpacked = unpacksigned(packed, bits, shift);
      int ok = (i == unpacked) && !((packed >> shift) & ~((1 << bits) - 1));
      if (0)
        printf("test1: i=%d shift=%d packed=%#llx unpacked=%lld ok=%d\n", i,
               shift, packed, unpacked, ok);
      fail += !ok;
    }

  if (fail)
    errx(-1, "testpacksigned failed");
}

struct Record {
  uint64_t packed;
  int32_t num;
  uint32_t name;
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

int Recordnamepresent(struct Record *r) { return r->name & 1; }
char *Recordname(struct threaddata *t, struct Record *r) {
  return t->names.data + ((r->name & 0xfffffe) >> 1);
}
int Recordnamesize(struct Record *r) { return r->name >> 24; }

#define TOTALBITS 41
int64_t Recordtotal(struct Record *r) {
  return unpacksigned(r->packed, TOTALBITS, 0);
}
void Recordsettotal(struct Record *r, int64_t total) {
  uint64_t mask = (1ULL << (1 + TOTALBITS)) - 1;
  r->packed = (r->packed & ~mask) | (packsigned(total, TOTALBITS, 0) & mask);
}

#define MINBITS 11
int16_t Recordmin(struct Record *r) {
  return unpacksigned(r->packed, MINBITS, TOTALBITS);
}
void Recordsetmin(struct Record *r, int16_t min) {
  int shift = TOTALBITS;
  uint64_t mask = ((1ULL << (1 + MINBITS)) - 1) << shift;
  r->packed = (r->packed & ~mask) | ((packsigned(min, MINBITS, shift)) & mask);
}

#define MAXBITS 11
int16_t Recordmax(struct Record *r) {
  return unpacksigned(r->packed, MAXBITS, TOTALBITS + MINBITS);
}
void Recordsetmax(struct Record *r, int16_t max) {
  int shift = TOTALBITS + MINBITS;
  uint64_t mask = ((1ULL << (1 + MAXBITS)) - 1) << shift;
  r->packed = (r->packed & ~mask) | ((packsigned(max, MAXBITS, shift)) & mask);
}

void testRecord(void) {
  struct {
    int64_t total;
    int16_t min, max;
  } * t, tests[] = {{0, 0, 0},
                    {1, 1, 1},
                    {-1, -1, -1},
                    {-999000000000, -999, -999},
                    {999000000000, 999, 999}};
  int fail;
  for (t = tests, fail = 0; t < endof(tests); t++) {
    struct Record r = {0};
    int ok;
    Recordsettotal(&r, t->total);
    Recordsetmin(&r, t->min);
    printf("r.packed=%#llx total=%lld r.min=%d\n", r.packed, Recordtotal(&r),
           Recordmin(&r));
    Recordsetmax(&r, t->max);
    printf("r.packed=%#llx total=%lld r.min=%d r.max=%d\n", r.packed,
           Recordtotal(&r), Recordmin(&r), Recordmax(&r));
    ok = Recordtotal(&r) == t->total && Recordmin(&r) == t->min &&
         Recordmax(&r) == t->max;
    if (!ok)
      printf("i=%ld fail: r.packed=%#llx\n", t - tests, r.packed);
    fail += !ok;
  }
  if (fail)
    errx(-1, "testRecord failed");
}

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

  for (i = ht_lookup(hash, EXP, hash);; i = (i + 1) % nelem(t->records)) {
    rp = t->records + i;
    if (!rp->name) {
      t->nrecords++;
      rp->name = namealloc(t, size + 1);
      memmove(rp->name, name, size);
      rp->name[size] = 0;
      return rp;
    } else if (equalmemstr(name, size, rp->name)) {
      return rp;
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
    testpacksigned();
    testRecord();
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
