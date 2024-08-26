#include <stdlib.h>
#include "jdf.h"

s8 blurb = text(This will be included with whitespace collapsed
                and trimmed
                and "quotes" escaped.
                );

arena malloc_arena(size cap) {
  arena a = {0}; // zero-initialise
  a.beg = malloc(cap);
  a.end = a.beg ? a.beg + cap : 0;
  return a;
}

size KiB(u32 n) {
  return (1<<10) * n;
}

size MiB(u32 n) {
  return (1<<20) * n;
}

// TODO more descriptive testing ??framework
int main(int argc, char *argv[]) {
  arena store = malloc_arena(KiB(2));
  s8 frag = s8("escaped.");
  s8 span = s8span(blurb.buf + (blurb.len - 8), blurb.buf + blurb.len);
  s8 slice = s8slice(blurb, -8, 0);
  u8 *found = s8find(blurb, s8("whitespace"));
  s8 found_to_end = s8span(found, blurb.buf + blurb.len);
  s8 trimmed = s8trim(s8("   escaped.                "));
  
  u8 *buf = new(&store, u8, KiB(1));
  bufout stdout[1] = {0};
  stdout->cap=64;
  stdout->buf = buf;
  s8writeln(stdout, s8("Demonstrate s8 string functions:"));
  s8writeln(stdout, blurb);
  s8writeln(stdout, frag);
  s8writeln(stdout, span);
  s8writeln(stdout, slice);
  s8writeln(stdout, found_to_end);
  s8writeln(stdout, trimmed);
  // compound literal initialising array of pointers to s8; type should be sized
  s8 *concs[] = {&frag, &found_to_end, &trimmed, &s8("all concatenated")};
  s8writeln(stdout, s8concat(&store, concs, countof(concs)));
  flush(stdout);
  oswrite(1, stdout->buf, stdout->len);

  assert(s8find(blurb, s8("quotes")));
  assert(!s8find(blurb, s8("nopey")));
  assert(s8equal(frag, span));
  assert(s8equal(frag, slice));
  assert(s8equal(frag, trimmed));
  
  error(0, s8("Finished"));
}
