#include "jdf.h"
#include <stdio.h>
#include <jansson.h>

arena store, scratch;

 // big alignment seems general 
void *store_alloc(usize count) {return alloc(&store, sizeof(byte), 8, count);}
void *scratch_alloc(usize count) {return alloc(&scratch, sizeof(byte), 8, count);}
void pretend_free(void *p) { (void)p; }

char fss(char c) {
  if (c == 0) return 0;
  if (c >= 'a' && c <= 'z') return c + ('A' - 'a');
  if (c >= 'A' && c <= 'Z') return c;
  return '_';
}

char *fussy_screaming_snake(arena *a, char *s, size len) {
  char *ret = new (a, char, len);
  for (size i = 0; i <= len; i++) ret[i] = fss(s[i]);
  return ret;
}

int main(int argc, char *argv[]) {
  store = alloc_arena(MiB(1));
  scratch = alloc_arena(MiB(1));
  FILE *f = {0};
  if (argc != 2) {perror("Specify one json file"); return 1;}
  f = fopen(argv[1], "r");
  if (!f) {perror("Unable to open"); goto exit;}
  json_set_alloc_funcs(scratch_alloc, pretend_free); // SEGV... 
  json_error_t error;
  json_t *root = json_load_file(argv[1], 0, &error);
  if (!root) {printf("%d: %s\n", error.line, error.text); goto exit;}
  const char *k;
  json_t *v;
  json_t *code;
  json_t *message;
  
  json_object_foreach(root, k, v) {
    code = json_object_get(v, "code");
    message = json_object_get(v, "message");
    if (json_is_integer(code) && json_is_string(message)) {
      i64 c = json_integer_value(code);
      const char *m = json_string_value(message);
      char *M = fussy_screaming_snake(&store, m, json_string_length(message));
      size len = json_string_length(message);
      printf("%lli %s %s\n", c, m, M);  
    }
  }
  printf("%ti bytes of scratch arena used", used(&scratch));
 exit:
  fclose(f);
  return errno;
}
