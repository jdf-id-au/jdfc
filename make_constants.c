#include "jdf.h"
#include "enum_tools.h"
#include <stdio.h>
#include <jansson.h>

arena store, scratch;

// big alignment should suit general use (?)
void *store_alloc(usize count) {return alloc(&store, sizeof(byte), 8, count);}
void *scratch_alloc(usize count) {return alloc(&scratch, sizeof(byte), 8, count);}
void pretend_free(void *p) { (void)p; }

int main(int argc, char *argv[]) {
  store = alloc_arena(MiB(1));
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
  json_set_alloc_funcs(scratch_alloc, pretend_free);
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
      size len = json_string_length(message);
      symbols[c] = fussy_screaming_snake(&store, (char *)m, len);
      messages[c] = m;
    }
  }

  
  
  fprintf(stderr, "\n%ti bytes of scratch arena used", used(&scratch));
 exit:
  fclose(f);
  return errno;
}
