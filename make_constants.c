#include "jdf.h"
#include <stdio.h>
#include <jansson.h>

// http: method: status: xxx: "Message"
// Title Case -> SCREAMING_SNAKE for enum name
// anything not [A-Za-z] -> _
int main(int argc, char *argv[]) {
  arena store = alloc_arena(MiB(1));
  arena scratch = alloc_arena(MiB(1));
  size bytes_read = 0;
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
  bytes_read = fread(scratch.cur, sizeof(byte), remaining(&scratch), f);
  if (ferror(f)) {
    perror("Unable to read");
    goto exit;
  }
  if (!feof(f)) {
    perror("File bigger than buffer");
    goto exit;
  }
  json_error_t error;
  json_t *root = json_loads(scratch.beg, 0, &error);
  if(!root) printf("%d: %s\n", error.line, error.text); 
  else printf("%zi bytes_read, %zu json_object_size\n", bytes_read, json_object_size(root));
 exit:
  fclose(f);
  return errno;
}
