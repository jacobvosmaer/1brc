#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define assert(x)                                                              \
  if (!(x))                                                                    \
  __builtin_trap()
#define nelem(x) (sizeof(x) / sizeof(*(x)))

struct city {
  char *name;
  double total, min, max;
  int num;
} cities[16 * 1024];
int ncities;

int citynameasc(const void *a_, const void *b_) {
  const struct city *a = a_, *b = b_;
  return strcmp(a->name, b->name);
}

void upsert(char *name, double temp) {
  int low, high, mid;
  struct city *c;
  low = 0;
  high = ncities;
  assert(strlen(name) > 0);
  while (low < high) {
    mid = (low + high) / 2;
    if (strcmp(name, cities[mid].name) > 0)
      low = mid + 1;
    else
      high = mid;
  }
  c = cities + low;
  if (c < cities + ncities && !strcmp(c->name, name)) {
    if (temp < c->min)
      c->min = temp;
    if (temp > c->max)
      c->max = temp;
    c->total += temp;
    c->num++;
    return;
  }
  assert(ncities < nelem(cities) - 1);
  memmove(c + 1, c, (ncities - (c - cities)) * sizeof(*cities));
  assert(c->name = strdup(name));
  c->min = c->max = c->total = temp;
  c->num = 1;
  ncities++;
}

char buf[256];

void printcities(void) {
  struct city *c;
  for (c = cities; c < cities + ncities; c++)
    printf("%s %.1f\n", c->name, c->max);
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
  struct city *c;

  if (0)
    testupsert();

  while (fgets(buf, sizeof(buf), stdin)) {
    char *p;
    double temp;
    assert(p = strchr(buf, ';'));
    *p = 0;
    assert(sscanf(p + 1, "%lf", &temp) == 1);
    upsert(buf, temp);
  }

  qsort(cities, ncities, sizeof(*cities), citynameasc);

  for (c = cities; c < cities + ncities; c++) {
    if (c == cities)
      printf("{%s=", c->name);
    else
      printf(", %s=", c->name);
    printf("%.1f/%.1f/%.1f", c->min, c->total / (double)c->num, c->max);
  }
  puts("}");

  return 0;
}
