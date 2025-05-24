#include <stdio.h>
#include <jansson.h>
#include "jdf.h"
#include "enum_tools.h"

arena *store, scratch;

// big alignment should suit general use (?)
void *store_alloc(usize count) { return alloc(store, sizeof(byte), 8, count, 0); }
void *scratch_alloc(usize count) { return alloc(&scratch, sizeof(byte), 8, count, 0); }
void pretend_free(void *p) { (void)p; }

s8 json_s8_value(json_t *s) {
  return (s8){.buf = (u8 *)json_string_value(s), .len = json_string_length(s) };
}

s8 symbolise(arena *store, json_t *s) {
  s8_ symbol = fussy_screaming_snake(store, json_s8_value(s));
  if (symbol.ok) return symbol.v;
  fprintf(stderr, "Invalid symbol: %s\n", json_string_value(s));
  exit(1); // rudely doesn't close fp
}

int main(int argc, char *argv[]) {
  arena storeval = alloc_arena(MiB(1));
  store = &storeval; // to make normal-looking fn calls
  scratch = alloc_arena(MiB(1));
  FILE *f = {0};
  if (argc != 2) {
    perror("Specify one json file");
    return 1;
  }
  f = fopen(argv[1], "r");
  if (!f) {
    perror("Unable to open");
    goto exit;
  }
  json_error_t error;
  json_set_alloc_funcs(store_alloc, pretend_free);
  json_t *root = json_load_file(argv[1], 0, &error);
  if (!root) {
    fprintf(stderr, "%d: %s\n", error.line, error.text);
    goto exit;
  }

  /*
    json should be map of enum name -> [value, ...]
    value should be any combination of:
    - symbol
    - [code, symbol]
    - [symbol, expansion]
    - [code, symbol, expansion]
    where code is integer and symbol and expansion are strings.
    Codes are passed to enum definition without validation.
    C will require "globally" unique enum symbols...
    Enums get INVALID_thing as first value, in enum_tools.h.
    Underlying enum type is the default; no attempt to narrow.
   */

  const char *j_id;
  size j_id_len;
  json_t *j_value_group, *j_value, *ja[3];
  usize i;
  s8enum *groups = 0;
  enum_values *head = 0;
  enum_values *values = 0;
  
  // TODO json error handling
  json_object_keylen_foreach(root, j_id, j_id_len, j_value_group) {
    s8 id = (s8){.buf = (u8 *)j_id, .len = j_id_len};
    json_array_foreach(j_value_group, i, j_value) {
      enum_value construct = {0};
      if (json_is_array(j_value)) {
        switch (json_array_size(j_value)) {
        case 3:
          ja[2] = json_array_get(j_value, 2);
          construct.text = json_s8_value(ja[2]);
        case 2: // deliberate fall-through!
          ja[0] = json_array_get(j_value, 0);
          ja[1] = json_array_get(j_value, 1);
          if (json_is_integer(ja[0])) {
            construct.number = json_integer_value(ja[0]);
            construct.name = json_s8_value(ja[1]);
            construct.symbol = symbolise(store, ja[1]);
            values = enum_valuesappend(store, values, construct);
          } else if (json_is_string(ja[0]) && json_array_size(j_value) == 2) {
            construct.name = json_s8_value(ja[0]);
            construct.symbol = symbolise(store,ja[0]);
            construct.text = json_s8_value(ja[1]);
            values = enum_valuesappend(store, values, construct);
          } else {
            fprintf(stderr, "Invalid combination: %s\n", json_dumps(j_value, 0));
          }
          break;
        default:
          fprintf(stderr, "Invalid value: %s\n", json_dumps(j_value, 0));
          goto exit;
        }
      } else if (json_is_string(j_value)) {
        construct.name = json_s8_value(j_value);
        construct.symbol = symbolise(store, j_value);
        construct.text = json_s8_value(j_value);
        values = enum_valuesappend(store, values, construct);
      } else {
        fprintf(stderr, "Invalid enum value definition: %s\n",
                json_dumps(j_value, 0));
        goto exit;
      }
      if (!head && values) head = values;
    }
    groups = s8enumassoc(store, groups, id, head);
    head = 0;
    values = 0;
  }

  bufout_ out = make_bufout(store, KiB(4), 1);
  if (!out.ok) {
    perror("Unable to allocate out buffer");
    goto exit;
  }
  s8write(&out.v, s8("// This is auto-generated, do not edit!\n"));
  s8write(&out.v, s8("#include \"jdf.h\"\n"));

  s8 source = s8wrap(argv[1], 256); // more conservative than FILENAME_MAX
  s8_ header_guard = fussy_screaming_snake(store, source); // e.g. SOMETHING_JSON
  assert(header_guard.ok);
  s8write(&out.v, s8("#ifndef "));
  s8write(&out.v, header_guard.v);
  s8write(&out.v, s8("\n"));
  s8write(&out.v, s8("#define "));
  s8write(&out.v, header_guard.v);
  s8write(&out.v, s8("\n"));
  
  do {
    render_enum(store, scratch, &out.v, groups->key, groups->val);
  } while ((groups = groups->next));

  s8write(&out.v, s8("#endif // "));
  s8write(&out.v, header_guard.v);
  s8write(&out.v, s8("\n"));
  flush(&out.v);
  
  fprintf(stderr, "\n%ti scratch and %ti store arena bytes used\n",
          used(&scratch), used(store));
      
 exit:
  fclose(f);
  return errno;
}
