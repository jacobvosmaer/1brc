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

#define EXP 16
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

uint64_t hash(char *key, int size) {
  uint64_t h = 3141592653;
  while (size--)
    h = 111 * h + (uint64_t)*key++;
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

struct city *upsert(char *name, int size) {
  uint64_t h = hash(name, size);
  int i = h;
  struct city *c;

  while (1) {
    i = ht_lookup(h, EXP, i);
    c = cityindex[i];

    if (!c) {
      assert(ncities < nelem(cities));
      c = cityindex[i] = cities + ncities++;
      assert(c->name = malloc(size + 1));
      memmove(c->name, name, size);
      c->name[size] = 0;
      return c;
    } else if (equalmemstr(name, size, c->name)) {
      return c;
    }
    collissions++;
  }
}

void printcities(void) {
  struct city *c;
  for (c = cities; c < cities + ncities; c++)
    printf("%s\n", c->name);
  putchar('\n');
}

void testupsert(void) {
  struct city *c, *abc, *def;

  abc = upsert("abc", 3);
  assert(ncities == 1);
  printcities();
  def = upsert("def", 3);
  assert(ncities == 2);
  printcities();
  assert(upsert("abc", 3) == abc);
  assert(ncities == 2);
  printcities();
  assert(upsert("def", 3) == def);
  assert(ncities == 2);
  printcities();
  upsert("012", 3);
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
    if (!(p = strchr(line, ';')))
      errx(-1, "missing ;");
    c = upsert(line, p - line);
    p++;
    if (*p == '-') {
      sign = -1;
      p++;
    }
    for (; *p && *p != '\n'; p++)
      if (*p != '.')
        temp = 10 * temp + digit(*p);
    temp *= sign;
    if (!c->num || temp < c->min)
      c->min = temp;
    if (!c->num || temp > c->max)
      c->max = temp;
    c->total += temp;
    c->num++;
    if (*p != '\n')
      errx(-1, "missing newline");
    line = p + 1; /* consume newline */
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
