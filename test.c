#include "jdf.h"
#include "test.h"
#include <stdio.h>

LIST(i32l, i32)
b32 i32eq(i32 a, i32 b) { return a == b; }
MAP_LIST(i32s8, i32, s8, i32eq)
SET_LIST(i32s, i32, i32eq)
SET_LIST(s8s, s8, s8equal)

#define STRINGS(...) ((char*[]){__VA_ARGS__})
#define ARGS(...) countof(STRINGS(__VA_ARGS__)), STRINGS(__VA_ARGS__)
#define ARGPARSE(defs, ...) argparse(&store, &scratch, defs, ARGS(__VA_ARGS__))
     
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
  TEST(s8find(haystack, s8("cd")) == haystack.abs + 2);
  TEST(!s8find(haystack, s8("g")));
  TEST(s8findu8(haystack, 'e') == haystack.abs + 4);
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
  PREP(s8 inty = s8("12345"));
  TEST(12345 == parse_i32(inty).v);

  PREP(arena store = alloc_arena(2, 0)); // stress test resize_arena
  PREP(arena scratch = alloc_arena(2, 0));

  HEAD("linked list");
  PREP(i32l *ll = i32l_append(&scratch, 0, 42));
  PREP(ll = i32l_append(&scratch, ll, 84));
  TEST(ll->val == 42);
  TEST(i32l_next(ll)->val == 84);
  TEST(i32l_nth(ll, 0)->val == 42);
  TEST(count(ll) == 2);
  
  HEAD("map (assocation) list");
  PREP(i32s8 *m = i32s8_assoc(&scratch, 0, 42, s8("meaning of life")));
  TEST(s8equal(i32s8_get(m, 42)->val, s8("meaning of life")));
  PREP(i32s8_assoc(&scratch, m, 84, s8("moar")));
  TEST(count(m) == 2);
  PREP(m = i32s8_dissoc(m, 42));
  TEST(!i32s8_get(m, 42));
  TEST(i32s8_get(m, 84));
  TEST(!i32s8_dissoc(m, 84));

  HEAD("set list");
  PREP(i32s *is = i32s_conj(&scratch, 0, 42));
  TEST(i32s_has(is, 42));
  TEST(!i32s_has(is, 84));

  PREP(s8s *ss = s8s_conj(&scratch, 0, s8("hello")));
  TEST(s8s_conj(&scratch, ss, s8("there")));
  TEST(s8s_has(ss, s8("hello")));
  
  printf("sizeof(s8s) %ti, scratch usage %ti B \n",
         sizeof(s8s), used(&scratch));

  HEAD("argparse");
  struct args a = {0};
  PREP(a = ARGPARSE("--port=int --workers=int", "pname", "--port=8080", "--workers=2", "--", "other",));
  
  TEST(8080 == int_arg(a, "port").v);
  TEST(2 == int_arg(a, "workers").v);
  TEST(s8equal(s8("other"), *plainargs_array_abs(a.rest)));

  // NB 2026-05-25 18:07:44 risking null pointer dereference on *x_arg() calls
  PREP(a = ARGPARSE("--port=int --workers=int", "pname", "--port=8080", "--workers=2", "other"));
  TEST(s8equal(s8("other"), *plainargs_array_abs(a.rest)));
  PREP(a = ARGPARSE("--port=int --workers=int", "pname", "other"));
  TEST(s8equal(s8("other"), *plainargs_array_abs(a.rest)));
  PREP(a = ARGPARSE( "--name=str", "pname", "--name", "thing"));
  TEST(s8equal(s8("thing"), str_arg(a, "name")));
  PREP(a = ARGPARSE("--yes=bool", "pname", "--yes"));
  TEST(1 == bool_arg(a, "yes").v);
  PREP(a = ARGPARSE("--yes=bool", "pname", "-y"));
  TEST(1 == bool_arg(a, "yes").v);
  PREP(a = ARGPARSE("--yes=bool", "pname", "-yfalse"));
  TEST(0 == bool_arg(a, "yes").v);
  PREP(a = ARGPARSE("--name=str", "pname", "-nhello"));
  TEST(s8equal(s8("hello"), str_arg(a, "name")));
  PREP(a = ARGPARSE("--name=str", "pname", "-n", "hello"));
  TEST(s8equal(s8("hello"), str_arg(a, "name")));
  PREP(a = ARGPARSE("--port=int --workers=int", "pname", "-p8080"));
  TEST(8080 == int_arg(a, "port").v);
  PREP(a = ARGPARSE("--port=int --workers=int", "pname", "-p 8080"));
  TEST(8080 == int_arg(a, "port").v);
  TEST(!int_arg(a, "absent").ok);
  // TODO 2026-05-25 17:26:36
  /* PREP(a = ARGPARSE( "--port=int --workers=int", "pname", "--port 8080")); */
  /* PREP(kv = kvargs_get(a.kv, s8("port"))); */
  /* TEST(8080 == *(i32 *)kv->val); */

  return REPORT();
}
