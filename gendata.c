#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t buf[256];

int main(void) {
  while (fgets((char *)buf, sizeof(buf), stdin))
    ;
}
