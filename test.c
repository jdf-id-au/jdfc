#include "jdf.h"
#include <stdio.h> // not reimplementing printf...

s8 blurb = text(This will be included with whitespace collapsed
                and trimmed
                and "quotes" escaped.
                );

b32 s8derefequal(s8 *a, s8 *b) {return a && b && s8equal(*a, *b);}

ASSOCIATION_LIST(s8, s8, s8derefequal)
LIST(s8rs, s8 *)

// TODO more descriptive testing ??framework
int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  arena store = alloc_arena(KiB(2));
  arena scratch = alloc_arena(MiB(1));

  u8 *end = endof(blurb);
  s8 frag = s8("escaped.");
  s8 span = s8span(blurb.buf + (blurb.len - 8), end);
  s8 slice = s8slice(blurb, -8, 0);
  u8 *found = s8find(blurb, s8("whitespace"));
  s8 found_to_end = s8span(found, end);
  u8 *f2 = s8findu8(blurb, '"');
  s8 f2_to_end = s8span(f2, end);
  s8 trimmed = s8trim(s8("   escaped.                "));

  bufout *stdout = bufout(&store, 64, 1);
  s8writeln(stdout, s8("Demonstrate s8 string functions:"));
  flush(stdout);
  s8writeln(stdout, blurb);
  s8writeln(stdout, frag);
  s8writeln(stdout, span);
  s8writeln(stdout, slice);
  s8writeln(stdout, found_to_end);
  s8writeln(stdout, f2_to_end);
  s8writeln(stdout, trimmed);
  // compound literal initialising array of pointers to s8; type should be sized
  s8 concs[] = {s8("concatenated s8s: "), frag, found_to_end, trimmed};
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

  s8 el = s8("hello s8build");
  s8build(&scratch, &el, &s8(" next"));
  s8writeln(stdout, s8arena(&scratch));
  flush(stdout);

  s8rs *rs = s8rsappend(&store, 0, &s8("first"));
  s8rs *tail = s8rsappend(&store, rs, &s8("second"));
  tail = s8rsappend(&store, tail, &s8("third"));
  tail = s8rsappend(&store, tail, &s8("fourth"));
  printf("rs has %ti entries\n", s8rscount(rs));
  
  s8s8 *al = s8s8assoc(&store, 0, rs->val, rs->next->val);
  s8s8assoc(&store, al, rs->next->next->val, rs->next->next->next->val);
  s8s8assoc(&store, al, &s8("first"), 0);
  s8 *match = s8s8get(al, &s8("third"));
  assert(match);
  s8writeln(stdout, *match);
  flush(stdout);
  
  failwith(0, s8("Finished"));
}
