#include <stdlib.h>
#include "jdf.h"

s8 blurb = text(This will be included with whitespace collapsed
                and trimmed
                and "quotes" escaped.
                );

// TODO more descriptive testing ??framework
int main(int argc, char *argv[]) {
  arena store = malloc_arena(KiB(2));
  u8 *end = lastof(blurb);
  s8 frag = s8("escaped.");
  s8 span = s8span(blurb.buf + (blurb.len - 8), end);
  s8 slice = s8slice(blurb, -8, 0);
  u8 *found = s8find(blurb, s8("whitespace"));
  u8 *f2 = s8find(blurb, s8(" "));
  s8 found_to_end = s8span(found, end);
  s8 f2_to_end = s8span(f2, end);
  s8 trimmed = s8trim(s8("   escaped.                "));
  
  bufout *stdout = bufout(&store, 64, 1);
  s8writeln(stdout, s8("Demonstrate s8 string functions:"));
  s8writeln(stdout, blurb);
  s8writeln(stdout, frag);
  s8writeln(stdout, span);
  s8writeln(stdout, slice);
  s8writeln(stdout, found_to_end);
  s8writeln(stdout, f2_to_end);
  s8writeln(stdout, trimmed);
  // compound literal initialising array of pointers to s8; type should be sized
  s8 concs[] = {frag, found_to_end, trimmed, s8("all concatenated")};
  s8writeln(stdout, s8concat(&store, concs, countof(concs)));
  s8writeln(stdout, s8concat(&store, (s8[]){s8("sadness "), s8("really")}, 2));
  flush(stdout);
  oswrite(1, stdout->buf, stdout->len);

  assert(s8find(blurb, s8("quotes")));
  assert(!s8find(blurb, s8("nopey")));
  assert(s8equal(frag, span));
  assert(s8equal(frag, slice));
  assert(s8equal(frag, trimmed));

  debug(s8("Debug a value:"));
  debytes(&(u64){0xabcd000012340000});
  error(0, s8("Finished"));
}
