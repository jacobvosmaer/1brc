#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define assert(x)                                                              \
  if (!(x))                                                                    \
  __builtin_trap()
#define nelem(x) (sizeof(x) / sizeof(*(x)))

#define EXP 16
struct city {
  char *name;
  double total, min, max;
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
      cityindex[i] = cities + ncities++;
      assert(cityindex[i]->name = strdup(name));
      return cityindex[i];
    } else if (!strcmp(name, cityindex[i]->name)) {
      return cityindex[i];
    }
    collissions++;
  }
}

char buf[256], stdinbuf[1 << 16];

void printcities(void) {
  struct city *c;
  for (c = cities; c < cities + ncities; c++)
    printf("%s %.1f\n", c->name, c->max);
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

int main(void) {
  struct city *c;

  if (0)
    assert(!setvbuf(stdin, stdinbuf, _IOFBF, sizeof(stdinbuf)));

  if (0) {
    testupsert();
    return 0;
  }

  while (fgets(buf, sizeof(buf), stdin)) {
    char *p;
    double temp;
    assert(p = strchr(buf, ';'));
    *p = 0;
    assert(sscanf(p + 1, "%lf", &temp) == 1);
    c = upsert(buf);
    if (!c->num || temp < c->min)
      c->min = temp;
    if (!c->num || temp > c->max)
      c->max = temp;
    c->total += temp;
    c->num++;
  }
  fprintf(stderr, "collissions=%d\n", collissions);

  qsort(cities, ncities, sizeof(*cities), citynameasc);

  for (c = cities; c < cities + ncities; c++)
    printf("%s%s=%.1f/%.1f/%.1f", c == cities ? "{" : ", ", c->name, c->min,
           c->total / (double)c->num, c->max);
  puts("}");

  return 0;
}
