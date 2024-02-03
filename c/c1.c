#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define assert(x)                                                              \
  if (!(x))                                                                    \
  __builtin_trap()

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
  struct city *c;
  for (c = cities; c < cities + ncities; c++) {
    if (!strcmp(c->name, name)) {
      if (temp < c->min)
        c->min = temp;
      if (temp > c->max)
        c->max = temp;
      c->total += temp;
      c->num++;
      return;
    }
  }
  assert(c->name = strdup(name));
  c->min = c->max = c->total = temp;
  c->num = 1;
  ncities++;
}

char buf[256];

int main(void) {
  struct city *c;
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
