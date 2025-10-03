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
  PREP(s8 source = s8("ab, cd, ef"));
  PREP(s8 target = s8(", "));
  PREP(s8a_ split = s8split(&store, scratch, source, target, 10););
  TEST(split.v.len == 3);
  TEST(s8equal(split.v.buf[1], s8("cd")));
  PREP(s8_ replaced = s8replace(&store, scratch, source, target, s8("other")));
  TEST(s8equal(replaced.v, s8("abothercdotheref")));
  PREP(replaced = s8replace(&store, scratch, source, s8("ab"), s8("_")));
  TEST(s8equal(replaced.v, s8("_, cd, ef")));
  PREP(replaced = s8replace(&store, scratch, source, s8("ef"), s8("_")));
  TEST(s8equal(replaced.v, s8("ab, cd, _")));

  HEAD("s8 cut");
  PREP(s8pair cut = s8cut(s8("ab, cd, ef"), s8(", ")));
  TEST(s8equal(cut.tail, s8("cd, ef")));
  PREP(cut = s8cutu8(s8("ab, cd, ef"), (u8)','));
  TEST(s8equal(cut.tail, s8(" cd, ef")));

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

  HEAD("argparse");

  char *argv[] = {"--port=8080", "--workers=2", "--", "other"}; // macros don't like designated initialisers
  PREP(struct args a = argparse(&store, countof(argv), argv, "--port=int --workers=int"));
  s8vm *kv = 0;
  PREP(kv = s8vmget(a.kv, s8("port")));
  TEST(8080 == *(i32 *)kv->val);
  PREP(kv = s8vmget(a.kv, s8("workers")));
  TEST(2 == *(i32 *)kv->val);
  TEST(s8equal(s8("other"), a.rest.buf[0]));
  
  char *argv2[] = {"--port=8080", "--workers=2", "other"};
  PREP(struct args a2 = argparse(&store, countof(argv2), argv2, "--port=int --workers=int"));
  TEST(s8equal(s8("other"), a2.rest.buf[0]));
  
  char *argv3[] = {"other"};
  PREP(struct args a3 = argparse(&store, countof(argv3), argv3, "--port=int --workers=int"));
  TEST(s8equal(s8("other"), a3.rest.buf[0]));
char *argv4[] = {"--name", "thing"};
  PREP(struct args a4 = argparse(&store, countof(argv4), argv4, "--name=str"));
  PREP(kv = s8vmget(a4.kv, s8("name")));
  TEST(kv);
  TEST(s8equal(s8("thing"), *(s8 *)kv->val));
  
  char *argv5[] = {"--yes"};
  PREP(struct args a5 = argparse(&store, countof(argv5), argv5, "--yes=bool"));
  PREP(kv = s8vmget(a5.kv, s8("yes")));
  TEST(kv);
  TEST(*(b32 *)kv->val);
  return REPORT();
}
