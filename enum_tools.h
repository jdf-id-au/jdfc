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
  char *ret = new (a, char, len);
  for (size i = 0; i <= len; i++) ret[i] = fss(s[i]); // include \0 terminator
  return ret;
}

typedef struct {
  i32 number;
  s8 symbol;
  s8 description;
} enum_value;

ARRAY(enum_values, enum_value)

b32 render_enum(arena scratch, bufout *b,
                s8 identifier, enum_values values) {
  // grug approve
  s8write(b, s8("enum "));
  s8write(b, identifier);
  
}

#endif // enum_tools_h
