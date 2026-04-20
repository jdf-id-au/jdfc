#include "jdf.h"
#include "test.h"
#include <stdio.h>

LIST(i32l, i32)
b32 i32eq(i32 a, i32 b) { return a == b; }
MAP_LIST(i32s8, i32, s8, i32eq)
SET_LIST(i32s, i32, i32eq)

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

  HEAD("s8 cut");
  PREP(s8pair cut = s8cut(s8("ab, cd, ef"), s8(", ")));
  TEST(s8equal(cut.tail, s8("cd, ef")));
  PREP(cut = s8cutu8(s8("ab, cd, ef"), (u8)','));
  TEST(s8equal(cut.tail, s8(" cd, ef")));

  // TODO etc...

  PREP(arena store = alloc_arena(KiB(2)));
  PREP(arena scratch = alloc_arena(KiB(1)));

  HEAD("linked list");
  PREP(i32l *ll = i32lappend(&scratch, 0, 42));
  PREP(i32lappend(&scratch, ll, 84));
  TEST(ll->val == 42);
  TEST(i32lnext(ll)->val == 84);
  TEST(i32lnth(ll, 0)->val == 42);
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
  PREP(i32s *is = i32sconj(&scratch, 0, 42));
  TEST(i32shas(is, 42));
  TEST(!i32shas(is, 84));

  PREP(s8s *ss = s8sconj(&scratch, 0, s8("hello")));
  TEST(s8sconj(&scratch, ss, s8("there")));
  TEST(s8shas(ss, s8("hello")));
  
  printf("sizeof(s8s) %ti, scratch usage %ti B \n",
         sizeof(s8s), used(&scratch));

  HEAD("argparse");

  char *argv[] = {"pname", "--port=8080", "--workers=2", "--", "other"}; // macros don't like designated initialisers
  PREP(struct args a = argparse(&store, countof(argv), argv, "--port=int --workers=int"));
  s8vm *kv = 0;
  PREP(kv = s8vmget(a.kv, s8("port")));
  TEST(8080 == *(i32 *)kv->val);
  PREP(kv = s8vmget(a.kv, s8("workers")));
  TEST(2 == *(i32 *)kv->val);
  TEST(s8equal(s8("other"), a.rest.buf[0]));
  
  char *argv2[] = {"pname", "--port=8080", "--workers=2", "other"};
  PREP(struct args a2 = argparse(&store, countof(argv2), argv2, "--port=int --workers=int"));
  TEST(s8equal(s8("other"), a2.rest.buf[0]));
  
  char *argv3[] = {"pname", "other"};
  PREP(struct args a3 = argparse(&store, countof(argv3), argv3, "--port=int --workers=int"));
  TEST(s8equal(s8("other"), a3.rest.buf[0]));

  char *argv4[] = {"pname", "--name", "thing"};
  PREP(struct args a4 = argparse(&store, countof(argv4), argv4, "--name=str"));
  PREP(kv = s8vmget(a4.kv, s8("name")));
  TEST(s8equal(s8("thing"), *(s8 *)kv->val));
  
  char *argv5[] = {"pname", "--yes"};
  PREP(struct args a5 = argparse(&store, countof(argv5), argv5, "--yes=bool"));
  PREP(kv = s8vmget(a5.kv, s8("yes")));
  TEST(*(b32 *)kv->val);

  char *argv6[] = {"pname", "-y"};
  PREP(struct args a6 = argparse(&store, countof(argv6), argv6, "--yes=bool"));
  PREP(kv = s8vmget(a6.kv, s8("yes")));
  TEST(*(b32 *)kv->val);

  char *argv7[] = {"pname", "-nhello"};
  PREP(struct args a7 = argparse(&store, countof(argv7), argv7, "--name=str"));
  PREP(kv = s8vmget(a7.kv, s8("name")));
  TEST(s8equal(s8("hello"), *(s8 *)kv->val));

  char *argv8[] = {"pname", "-n", "hello"};
  PREP(struct args a8 = argparse(&store, countof(argv8), argv8, "--name=str"));
  PREP(kv = s8vmget(a8.kv, s8("name")));
  TEST(s8equal(s8("hello"), *(s8 *)kv->val));

  char *argv9[] = {"pname", "-p8080"};
  PREP(struct args a9 = argparse(&store, countof(argv9), argv9, "--port=int --workers=int"));
  PREP(kv = s8vmget(a9.kv, s8("port")));
  TEST(8080 == *(i32 *)kv->val);

  return REPORT();
}
