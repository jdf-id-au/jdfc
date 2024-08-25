#include <stdlib.h>
#include "jdf.h"

s8 blurb = text(
                This will be included with whitespace collapsed
                and "quotes" escaped.
                );

int main(int argc, char *argv[]) {
  i32 cap = 1 << 10;
  arena a = {0}; // zero-initalise
  a.beg = malloc(cap);
  // ugh Apple https://stackoverflow.com/questions/64126942/malloc-nano-zone-abandoned-due-to-inability-to-preallocate-reserved-vm-space
  a.end = a.beg + cap;

  i32 bcap = 1 << 10;
  u8 *buf = new(&a, u8, bcap);

  bufout stdout[1] = {0}; // zero-initialise single bufout struct and return pointer to it
  stdout->cap = 10;
  stdout->buf = buf;
  s8write(stdout, blurb);
  flush(stdout);

  oswrite(1, stdout->buf, stdout->len); // 1 is os stdout fd, nominally "implementation-defined"?
}
