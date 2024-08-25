#include <stdlib.h>
#include "jdf.h"

s8 blurb = text(
                This will be included with whitespace collapsed
                and "quotes" escaped.
                );

arena malloc_arena(size cap) {
  arena a = {0}; // zero-initialise
  a.beg = malloc(cap);
  a.end = a.beg ? a.beg + cap : 0;
  return a;
}

int main(int argc, char *argv[]) {
  arena store = malloc_arena(1 << 10);
  byte *memory = store.beg; // unnecessary here https://nullprogram.com/blog/2023/09/27/

  i32 bcap = 1 << 10;
  u8 *buf = new(&store, u8, bcap);

  bufout stdout[1] = {0}; // zero-initialise single bufout struct and return pointer to it
  stdout->cap = 10;
  stdout->buf = buf;
  s8write(stdout, blurb);
  flush(stdout);

  oswrite(1, stdout->buf, stdout->len); // 1 is os stdout fd, nominally "implementation-defined"?
  free(memory); // unnecessary see above
}
