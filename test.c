#include "jdf.h"
#include "test.h"
#include <stdio.h>

LIST(i32s, i32)
b32 i32eq(i32 a, i32 b) { return a == b; }
MAP_LIST(i32s8, i32, s8, i32eq)
SET_LIST(i32set, i32, i32eq)
SET_LIST(s8set, s8, s8equal)

i32 main(void) {
  HEAD("s8 string functions");
  TEST(s8equal(s8("hello"), s8wrap("hello", 10)));
  TEST(s8equal(s8("inner"), s8slice(s8("hello inner world"), 6, 11)));
  TEST(s8equal(s8("inner"), s8slice(s8("hello inner world"), 6, -6)));  
  TEST(s8equal(s8("inner"), s8slice(s8("hello inner world"), -11, 11)));  
  TEST(s8cmp(s8("a"), s8("b")) == -1);
  TEST(s8cmp(s8("b"), s8("a")) == 1);
  TEST(s8hash(s8("a")) != s8hash(s8("b")));
  PREP(s8 haystack = s8("abcdef"));
  TEST(s8find(haystack, s8("cd")) == haystack.buf + 2);
  TEST(!s8find(haystack, s8("g")));
  TEST(s8findu8(haystack, 'e') == haystack.buf + 4);
  TEST(s8startswith(haystack, s8("abc")));
  TEST(s8endswith(haystack, s8("def")));
  TEST(s8equal(s8("hello"), s8trim(s8("       hello\t\r\n\v\f"))));
  TEST(s8blank(s8("  \t\r\v\n\f  "
                  "    ")));

  HEAD("s8 split, using arena");
  PREP(arena store = alloc_arena(KiB(2)));
  PREP(arena scratch = alloc_arena(KiB(1)));
  PREP(s8a_ split = s8split(&store, scratch, s8("ab, cd, ef"), s8(", "), 10););
  TEST(split.v.len == 3);
  TEST(s8equal(split.v.buf[1], s8("cd")));

  // TODO etc...

  HEAD("linked list");
  PREP(i32s *ll = i32sappend(&scratch, 0, 42));
  PREP(i32sappend(&scratch, ll, 84));
  TEST(ll->val == 42);
  TEST(i32snext(ll)->val == 84);
  TEST(i32snth(ll, 0)->val == 42);
  TEST(count(ll) == 2);
  
  HEAD("map (assocation) list");
  PREP(i32s8 *m = i32s8assoc(&scratch, 0, 42, s8("meaning of life")));
  TEST(s8equal(i32s8get(m, 42)->val, s8("meaning of life")));
  PREP(i32s8assoc(&scratch, m, 84, s8("moar")));
  TEST(count(m) == 2);
  PREP(m = i32s8dissoc(m, 42));
  TEST(!i32s8get(m, 42));
  TEST(i32s8get(m, 84));
  TEST(!i32s8dissoc(m, 84));

  HEAD("set list");
  PREP(i32set *is = i32setconj(&scratch, 0, 42));
  TEST(i32sethas(is, 42));
  TEST(!i32sethas(is, 84));

  PREP(s8set *ss = s8setconj(&scratch, 0, s8("hello")));
  TEST(s8setconj(&scratch, ss, s8("there")));
  TEST(s8sethas(ss, s8("hello")));
  
  printf("sizeof(s8set) %ti, scratch usage %ti B \n",
         sizeof(s8set), used(&scratch));

  return REPORT();
}
