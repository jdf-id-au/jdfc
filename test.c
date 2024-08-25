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

size KiB(u32 k) {
  return (1<<10) * k;
}

size MiB(u32 k) {
  return (1<<20) * k;
}

int main(int argc, char *argv[]) {
  arena store = malloc_arena(KiB(1));
  s8 frag = s8("escaped.");
  s8 span = s8span(blurb.buf + (blurb.len - 8), blurb.buf + blurb.len);
  s8 slice = s8slice(blurb, -8, 0);
  
  u8 *buf = new(&store, u8, KiB(1));
  bufout stdout[1] = {0};
  stdout->cap=64;
  stdout->buf = buf;
  s8writeln(stdout, frag);
  s8writeln(stdout, span);
  s8writeln(stdout, slice);
  flush(stdout);
  oswrite(1, stdout->buf, stdout->len);
  
  assert(!s8cmp(frag, span));
  assert(!s8cmp(frag, slice));
}
