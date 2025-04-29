#include "jdf.h"
#include <stdio.h> // not reimplementing printf...

s8 blurb = text(This will be included with whitespace collapsed
                and trimmed
                and "quotes" escaped.
                );

ASSOCIATION_LIST(s8map, s8, s8, s8equal)
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


  // bufout *stdout = bufout(&store, 64, 1);
  // s8write(stdout, s8("Demonstrate s8 string functions:"));
  // flush(stdout);

  log_debug(blurb);
  log_debug(frag);
  log_debug(span);
  log_debug(slice);
  
  log_debug(blurb);
  log_debug(frag);
  log_debug(span);
  log_debug(slice);
  log_debug(found_to_end);
  log_debug(f2_to_end);
  log_debug(trimmed);
  // compound literal initialising array of pointers to s8; type should be sized
  s8 concs[] = {s8("concatenated s8s: "), frag, found_to_end, trimmed};
  log_debug(s8concat(&store, concs, countof(concs)));
  log_debug(s8concat(&store, (s8[]){s8("sadness "), s8("really")}, 2));
  
  assert(s8find(blurb, s8("quotes")));
  assert(!s8find(blurb, s8("nopey")));
  assert(s8equal(frag, span));
  assert(s8equal(frag, slice));
  assert(s8equal(frag, trimmed));

  inspect(&(u64){0xabcd000012340000});

  s8 el = s8("hello s8build");
  // NB normally scratch arena is passed by value, but it's functioning as a buffer here...
  s8build(&scratch, &el, &s8(" next"));
  log_debug(s8arena(&scratch));
  
  s8rs *rs = s8rsappend(&store, 0, &s8("first"));
  s8rs *tail = s8rsappend(&store, rs, &s8("second"));
  tail = s8rsappend(&store, tail, &s8("third"));
  tail = s8rsappend(&store, tail, &s8("fourth"));
  tail = s8rsappend(&store, tail, &s8("fifth"));
  tail = s8rsappend(&store, tail, &s8("sixth"));
  printf("rs has %ti entries\n", s8rscount(rs));

  s8map *al = s8mapassoc(&store, 0, s8("a"), s8("b"));
  al = s8mapassoc(&store, al, s8("c"), s8("d"));
  al = s8mapassoc(&store, al, s8("e"), s8("f"));
  al = s8mapassoc(&store, al, s8("g"), s8("h"));
  
  printf("al has %ti entries\n", s8mapcount(al));
  al = s8mapdissoc(al, s8("a"));
  printf("al now has %ti entries\n", s8mapcount(al));

  s8map *match = s8mapget(al, s8("e"));
  if(match) log_debug(match->val);

  // stupid example of stack allocated arena and s8 split
  // should normally both be heap allocated
  byte beg[1024] = {0};
  arena tmp = {.beg = beg, .cur = beg, .end = beg + 1024};
  s8 mess = s8("this, and that, and the other");
  s8s spl = s8splitu8(&tmp, scratch, mess, ',', 4);
  for (size i = 0; i < spl.len; i++)
    log_debug(spl.buf[i]);

  assert(s8endswith(s8("thing some"), s8("some")));
  assert(s8startswith(s8("thing some"), s8("thing")));
  
  failwith(0, s8("Finished"));
}
