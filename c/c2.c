#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define assert(x)                                                              \
  if (!(x))                                                                    \
  __builtin_trap()
#define nelem(x) (sizeof(x) / sizeof(*(x)))

#define EXP 14
struct city {
  char *name;
  double total, min, max;
  int num;
} cities[1 << EXP];
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

uint64_t hash(char *key) {
  uint64_t h = 0;
  while (*key)
    h = 23 * h + *key++;
  return h;
}

void upsert(char *name, double temp) {
  uint64_t h = hash(name);
  int i;
  struct city *c;

  for (i = h;;) {
    i = ht_lookup(h, EXP, i);
    if (!cities[i].name || !strcmp(name, cities[i].name))
      break;
  }

  c = cities + i;
  if (c->name && !strcmp(c->name, name)) {
    if (temp < c->min)
      c->min = temp;
    if (temp > c->max)
      c->max = temp;
    c->total += temp;
    c->num++;
  } else {
    assert(c->name = strdup(name));
    c->min = c->max = c->total = temp;
    c->num = 1;
    ncities++;
  }
}

char buf[256], stdinbuf[1 << 16];

void printcities(void) {
  struct city *c;
  for (c = cities; c < cities + nelem(cities); c++)
    if (c->name)
      printf("%s %.1f\n", c->name, c->max);
  putchar('\n');
}

void testupsert(void) {
  struct city *c;

  upsert("abc", 1);
  assert(ncities == 1);
  printcities();
  upsert("def", 2);
  assert(ncities == 2);
  printcities();
  upsert("abc", 3);
  assert(ncities == 2);
  printcities();
  upsert("def", 4);
  assert(ncities == 2);
  printcities();
  upsert("012", 5);
  assert(ncities == 3);
  printcities();

  for (c = cities; c < cities + nelem(cities); c++)
    free(c->name);
  ncities = 0;
}

int main(void) {
  struct city *c, *sortcities;
  int i;

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
    upsert(buf, temp);
  }

  assert(sortcities = malloc(ncities * sizeof(*sortcities)));
  for (c = cities, i = 0; c < cities + nelem(cities); c++)
    if (c->name)
      sortcities[i++] = *c;
  assert(i == ncities);

  qsort(sortcities, ncities, sizeof(*sortcities), citynameasc);

  for (c = sortcities; c < sortcities + ncities; c++)
    printf("%s%s=%.1f/%.1f/%.1f", c == sortcities ? "{" : ", ", c->name, c->min,
           c->total / (double)c->num, c->max);
  puts("}");

  return 0;
}
