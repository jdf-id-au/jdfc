#include "jdf.h"
#include "enum_tools.h"
#include <stdio.h>
#include <jansson.h>

arena *store, scratch;

// big alignment should suit general use (?)
void *store_alloc(usize count) { return alloc(store, sizeof(byte), 8, count, 0); }
void *scratch_alloc(usize count) { return alloc(&scratch, sizeof(byte), 8, count, 0); }
void pretend_free(void *p) { (void)p; }

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
  const char *k;
  json_t *v;
  json_t *code;
  json_t *message;
  
  char const *symbols[600] = {0};
  char const *messages[600] = {0};
  json_object_foreach(root, k, v) {
    code = json_object_get(v, "code");
    message = json_object_get(v, "message");
    if (json_is_integer(code) && json_is_string(message)) {
      i64 c = json_integer_value(code);
      const char *m = json_string_value(message);
      size len = json_string_length(message); // excludes \0 terminator
      symbols[c] = fussy_screaming_snake(store, (char *)m, len);
      messages[c] = m;
    }
  }

  enum_values *vs = 0;
  enum_values *cur = vs; // hopefully this copies vs pointer to cur and leaves vs
  for (size i = 0; i < countof(symbols); i++) {
    if (symbols[i]) {
      cur = enum_valuesappend(
          store, cur,
          (enum_value){.number = i,
                       .symbol = s8wrap(symbols[i], 100),
                       .description = s8wrap(messages[i], 100)});
      if (!vs) vs = cur;
    }
  }
  bufout_ out = make_bufout(store, KiB(4), 1);
  if (!out.ok) {
    perror("Unable to allocate out buffer");
    goto exit;
  }
  return render_enum(scratch, &out.v, s8("http_status"), vs);
  fprintf(stderr, "\n%ti scratch and %ti store arena bytes used",
          used(&scratch), used(store));
      
 exit:
  fclose(f);
  return errno;
}
