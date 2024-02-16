#include <err.h>
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

struct city {
  char *name;
  int64_t total, min, max;
  int num;
} cities[1 << 14], *cityindex[1 << EXP];
int ncities;

int citynameasc(const void *a_, const void *b_) {
  const struct city *a = a_, *b = b_;
  return strcmp(a->name, b->name);
}

/* From https://nullprogram.com/blog/2022/08/08/ */
int ht_lookup(uint64_t hash, int exp, int idx) {
  uint32_t mask = ((uint32_t)1 << exp) - 1;
  uint32_t step = (hash >> (64 - exp)) | 1;
  return (idx + step) & mask;
}

int collissions;

uint64_t hash(char *key) {
  uint64_t h = 0;
  while (*key)
    h = 111 * h + (uint64_t)*key++;
  return h;
}

struct city *upsert(char *name) {
  uint64_t h = hash(name);
  int i = h;

  while (1) {
    i = ht_lookup(h, EXP, i);

    if (!cityindex[i]) {
      assert(ncities < nelem(cities));
      cityindex[i] = cities + ncities++;
      assert(cityindex[i]->name = strdup(name));
      return cityindex[i];
    } else if (!strcmp(name, cityindex[i]->name)) {
      return cityindex[i];
    }
    collissions++;
  }
}

char buf[256];

void printcities(void) {
  struct city *c;
  for (c = cities; c < cities + ncities; c++)
    printf("%s\n", c->name);
  putchar('\n');
}

void testupsert(void) {
  struct city *c, *abc, *def;

  abc = upsert("abc");
  assert(ncities == 1);
  printcities();
  def = upsert("def");
  assert(ncities == 2);
  printcities();
  assert(upsert("abc") == abc);
  assert(ncities == 2);
  printcities();
  assert(upsert("def") == def);
  assert(ncities == 2);
  printcities();
  upsert("012");
  assert(ncities == 3);
  printcities();

  for (c = cities; c < cities + nelem(cities); c++)
    free(c->name);
  ncities = 0;
  memset(cityindex, 0, sizeof(cityindex));
}

int digit(char c) {
  assert(c >= '0' && c <= '9');
  return c - '0';
}

int main(void) {
  struct city *c;
  struct stat st;
  char *in, *line;

  if (0) {
    testupsert();
    return 0;
  }
  if (fstat(0, &st))
    err(-1, "fstat stdin");
  if (!(in = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, 0, 0)))
    err(-1, "mmap stdin");

  for (line = in; line < in + st.st_size;) {
    char *p;
    int64_t temp = 0, sign = 1;
    assert(p = strchr(line, ';'));
    assert(p - line < sizeof(buf) - 1);
    memmove(buf, line, p - line);
    buf[p - line] = 0;
    p++;
    if (*p == '-') {
      sign = -1;
      p++;
    }
    for (; *p && *p != '\n'; p++)
      if (*p != '.')
        temp = 10 * temp + digit(*p);
    temp *= sign;
    c = upsert(buf);
    if (!c->num || temp < c->min)
      c->min = temp;
    if (!c->num || temp > c->max)
      c->max = temp;
    c->total += temp;
    c->num++;
    if (!(line = memchr(line, '\n', st.st_size - (line - in))))
      errx(-1, "missing newline");
    line++; /* consume newline */
  }
  fprintf(stderr, "collissions=%d\n", collissions);

  /* This qsort will invalidate cityindex but that is OK because we don't need
   * cityindex anymore. */
  qsort(cities, ncities, sizeof(*cities), citynameasc);

  for (c = cities; c < cities + ncities; c++)
    printf("%s%s=%.1f/%.1f/%.1f", c == cities ? "{" : ", ", c->name,
           (double)c->min / 10.0, (double)c->total / (10.0 * (double)c->num),
           (double)c->max / 10.0);
  puts("}");

  return 0;
}
