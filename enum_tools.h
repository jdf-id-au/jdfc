#include "jdf.h"
#ifndef enum_tools_h
#define enum_tools_h

char fss(char c) {
  if (c == 0) return 0;
  if (c >= 'a' && c <= 'z') return c + ('A' - 'a');
  if (c >= 'A' && c <= 'Z') return c;
  return '_';
}

char *fussy_screaming_snake(arena *a, char *s, size len) {
  char *ret = new (a, char, len + 1); // inculde \0 terminator
  for (size i = 0; i <= len; i++) ret[i] = fss(s[i]); // include \0 terminator
  return ret;
}

typedef struct {
  i32 number;
  s8 symbol;
  s8 description;
} enum_value;

LIST(enum_values, enum_value)

b32 render_enum(arena scratch, bufout *b, s8 identifier, enum_values *values) {
  s8 ind = s8("  ");
  // grug approve
  s8write(b, s8("enum http_status {\n"));
  enum_values *cur = values;
  do {
    // grug approve
    s8write(b, ind);
    s8write(b, cur->val.symbol);
    s8printf(scratch, s8write, b, " = %i,\n", cur->val.number); // trailing comma ok in C99
  } while ((cur = cur->next));
  s8write(b, s8("};\n"));
  flush(b);
}

#endif // enum_tools_h
