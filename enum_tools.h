#include "jdf.h"
#ifndef enum_tools_h
#define enum_tools_h

char fss(char c) {
  if (c == 0) return 0;
  if (c >= 'a' && c <= 'z') return c + ('A' - 'a');
  if (c >= 'A' && c <= 'Z') return c;
  return '_';
}

s8_ fussy_screaming_snake(arena *store, s8 s) {
  s8_ ret = make_s8(store, s.len);
  assert(ret.ok);
  for (size i = 0; i < s.len; i++) ret.v.buf[i] = fss(s.buf[i]);
  return ret;
}

s8_ sanitise(arena *store, arena scratch, s8 s) {
  size fixes = 0; // to add one character per target for escaping
  char targets[] = "\\\"";
  for (size i = 0; i < s.len; i++) 
    for (usize j = 0; j < countof(targets) - 1; j++)
      if (s.buf[i] == targets[j])
        fixes++;
  s8_ ret = make_s8(store, s.len + fixes);
  assert(ret.ok);
  size r = 0;
  for (size i = 0; i < s.len; i++) {
    b32 targeted = 0;
    for (usize j = 0; j < sizeof(targets); j++)
      if (s.buf[i] == targets[j])
        targeted = 1;
    if (targeted) ret.v.buf[r++] = '\\';
    ret.v.buf[r++] = s.buf[i];
  }
  return ret;
}

typedef struct {
  i32 number;
  s8 name;
  s8 symbol;
  s8 text;
} enum_value;

LIST(enum_values, enum_value)
MAP_LIST(s8enum, s8, enum_values *, s8equal)

#define W(x) s8write(b, x);
#define S(x) W(s8(x));

// Refuse to use X macro...
void render_enum(arena *store, arena scratch, bufout *b, s8 id, enum_values *values) {
  s8 ind = s8("  ");
  s8_ ID = fussy_screaming_snake(store, id);
  // grug approve
  S("enum "); W(id); S(" {\n");
  S("  INVALID_"); W(ID.v); S(",\n"); // always first i.e. 0
  enum_values *cur = values;
  do { 
    W(ind);
    W(cur->val.symbol);
    if (cur->val.number != 0) // actually prevent explicit assignment to 0!
      s8printf(scratch, s8write, b, " = %i,\n",
               cur->val.number); // trailing comma ok in C99
    else S(",\n");
  } while ((cur = cur->next));
  S("};\n");
  
  S("const char *spell_"); W(id); S("[] = {\n");
  cur = values;
  do {
    W(ind);
    S("[");
    W(cur->val.symbol);
    S("] = \"");
    s8_ sanname = sanitise(store, scratch, cur->val.name);
    W(sanname.v);
    S("\",\n");
  } while ((cur = cur->next));
  S("};\n");
  
  S("const char *describe_"); W(id); S("[] = {\n");
  cur = values;
  do {
    W(ind);
    S("[");
    W(cur->val.symbol);
    S("] = \"");
    s8_ santext = sanitise(store, scratch, cur->val.text);
    W(santext.v);
    S("\",\n");
  } while ((cur = cur->next));
  S("};\n");
  
  S("enum "); W(id); S(" parse_"); W(id); S("(s8 s) {\n");
  s8printf(scratch, s8write, b,
           "  for (size i = 0; i < %ti; i++)\n", count(values));
  S("    if(s8equal(s, s8wrap(spell_"); W(id); S("[i], 1024)))\n");
  S("      return (enum "); W(id); S(")i;\n");
  S("  return (enum "); W(id); S(")0;\n"); // should be INVALID_<ID>
  S("}\n");
  flush(b);
}

#endif // enum_tools_h
