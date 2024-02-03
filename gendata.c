#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define nelem(x) (sizeof(x) / sizeof(*(x)))
#define assert(x)                                                              \
  if (!(x))                                                                    \
  __builtin_trap()

char buf[256], *pickcities[10000];

void fail(char *msg) {
  fprintf(stderr, "gendata: %s\n", msg);
  exit(1);
}

int main(int argc, char **argv) {
  char **cities = 0;
  int ncities = 0, maxcities = 0, nrows, npicked;

  if (argc != 2)
    fail("Usage: gendata NUM");
  nrows = atoi(argv[1]);
  if (nrows < 0)
    fail("number of rows must be non-negative");
  srandom(getpid());

  while (fgets(buf, sizeof(buf), stdin)) {
    char *p;
    if (*buf == '#')
      continue;
    if (ncities == maxcities) {
      maxcities = maxcities ? 2 * maxcities : 1;
      assert(cities = realloc(cities, maxcities * sizeof(*cities)));
    }
    assert(p = strchr(buf, ';'));
    *p = 0;
    assert(cities[ncities++] = strdup(buf));
  }

  assert(ncities > nelem(pickcities));
  for (npicked = 0; npicked < nelem(pickcities); npicked++) {
    int i = random() % ncities;
    pickcities[npicked] = cities[i];
    memmove(cities + i, cities + i + 1, ncities - (i + 1));
    ncities--;
  }

  while (nrows--) {
    float temp = (-999 + (random() % 1999)) / 10.0;
    printf("%s;%.1f\n", pickcities[random() % nelem(pickcities)], temp);
  }
}
