#include "jdf.h"

void oom(void) {
  static u8 msg[] = "out of memory\n";
  oswrite(2, (u8 *)msg, lengthof(msg));
  osfail(12); // cheesy reference to ENOMEM errno
}

size KiB(u32 n) {
  return (1<<10) * n;
}

size MiB(u32 n) {
  return (1<<20) * n;
}

// TODO could report memory usage afterward
byte *alloc(arena *a, size objsize, size align, size count) {
  size avail = a->end - a->beg;
  size padding = -(uptr)a->beg & (align - 1);
  if (count > (avail - padding)/objsize) oom();
  size total = count * objsize;
  byte *p = a->beg + padding;
  a->beg += padding + total;
  for (size i = 0; i < total; i++) p[i] = 0;
  return p;
}

void copy(u8 *restrict dst, u8 *restrict src, size len) {
  for (size i = 0; i < len; i++) dst[i] = src[i];
}

// ───────────────────────────────────────────────────────────────────── Strings

s8 s8span(u8 *beg, u8 *end) {
  if (beg && end && end > beg) return (s8){.buf = beg, .len = end - beg};
  return (s8){0};
}

s8 s8slice(s8 src, size from, size to) {
  s8 s = {.buf = src.buf};
  size f = (from < 0) ? src.len + from : from;
  size t = (to > 0) ? to : src.len + to;
  if (t < f) return s; // refuse to slice backwards
  s.buf += f;
  s.len = t - f;
  return s;
}

b32 s8equal(s8 a, s8 b) {
  if (a.len != b.len) return 0;
  for (size i = 0; i < a.len ; i++) if (a.buf[i] != b.buf[i]) return 0;
  return 1;
}

size s8cmp(s8 a, s8 b) {
  size len = (a.len < b.len) ? a.len : b.len;
  for (size i = 0; i < len; i++) {
    size d = a.buf[i] - b.buf[i];
    if (d) return d;
  }
  return a.len - b.len;
}

// Why `size`?
size s8hash(s8 s) {
  u64 h = 0x100;
  for (size i = 0; i < s.len; i++) {
    h ^= s.buf[i];
    h *= 1111111111111111111u; // nineteen ones
  }
  return (h ^ h>>32) & (u32)-1;
}

u8 *s8find(s8 haystack, s8 needle) {
  if (!haystack.buf || !needle.buf) return 0;
  u8 *found = 0;
  u8 *he = endof(haystack);
  u8 *ne = endof(needle);
  // init first; cond before loop; iter after loop
  for (u8 *h = haystack.buf; !found && (h < he); h++) {
    for (u8 *n = needle.buf;
         n < ne && h < he;
         n++) {
      if (*h == *n) {
        if (!found) found = h;
        h++;
      } else {
        if (found) h = found;
        found = 0;
        break;
      }
    }
  }
  return found;
}

u8 *s8findc(s8 haystack, u8 needle) {
  if (!haystack.buf) return 0; // allow \0 needle
  u8 *end = endof(haystack);
  for (u8 *h = haystack.buf; h < end; h++)
    if (*h == needle)
      return h;
  return 0;
}

s8 s8wrap(const char *cstr, size maxlen) {
  if (!cstr) return (s8){0};
  u8 *beg = (u8 *)cstr;
  u8 *end = beg;
  while (*end != '\0' && (end-beg) < maxlen) end++;
  return s8span(beg, end); 
}

// https://www.reddit.com/r/C_Programming/comments/kzouxh/isspace_ctypeh_considered_harmful/
// e.g. /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/ctype.h
b32 whitespace(u8 c) { // too cool for ctype.h isspace
  switch (c) {
  case ' ':
  case '\t':
  case '\v':
  case '\n':
  case '\r':
  case '\f':
    return 1;
  }
  return 0;
}

s8 s8trim(s8 src) {
  u8 *beg = src.buf;
  u8 *end = endof(src);
  while (beg < end && whitespace(*beg)) beg++;
  while (end > beg && whitespace(*(end - 1))) end--;
  return s8span(beg, end);
}

s8 s8fill(arena *a, u8 with, size count) {
  u8 *buf = new (a, u8, count);
  for (size i = 0; i < count; i++) *(buf + i) = with;
  return (s8){.buf = buf, .len = count};
}

s8 s8clone(arena *a, s8 s) {
  s8 c = (s8) {
    .buf = new (a, u8, s.len),
    .len = s.len
  };
  copy(c.buf, s.buf, s.len);
  return c;
}

s8 s8concat(arena *a, s8 *ss, size len) {
  size tot = 0;
  for (size i = 0; i < len; i++) tot += ss[i].len;
  u8 *buf = new(a, u8, tot);
  u8 *beg = buf;
  for (size i = 0; i < len; i++) {
    copy(beg, ss[i].buf, ss[i].len);
    beg += ss[i].len;
  }
  return (s8){.buf = buf, .len = tot};
}

void s8write(bufout *b, s8 s) {
  if (!s.buf) return;
  u8 *buf = s.buf;
  u8 *end = endof(s);
  while (!b->err && (buf < end)) {
    i32 avail = b->cap - b->len;
    i32 count = (avail < end - buf) ? avail : (i32)(end - buf);
    copy(b->buf + b->len, buf, count);
    buf += count;
    b->len += count;
    if (b->len == b->cap) flush(b);
  }
}

void s8writeln(bufout *b, s8 s) {
  s8write(b, s);
  s8write(b, s8("\n"));
}

// ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ List of strings

count_impl(s8s)
value_append_impl(s8s, s8) // corresponding to `value_list(s8s, s8)` in header

s8s *s8swrap(arena *a, const char **cstrs, size nstrs, size maxlen) {
  s8s *head = 0;
  s8s *cur = 0;
  for (size i = 0; i < nstrs; i++) {
    cur = s8sappend(a, head, s8wrap(cstrs[i], maxlen));
    head = head ? head : cur;
  }
  return head;
}
     
s8 s8sconcat(arena *a, s8s *ss) {
  assert(ss);
  size tot = 0;
  s8s *cur = ss;
  do {
    tot += cur->val.len;
  } while ((cur = cur->next));
  u8 *buf = new(a, u8, tot);
  u8 *beg = buf;
  cur = ss;
  do {
    copy(beg, cur->val.buf, cur->val.len);
    beg += cur->val.len;
  } while ((cur = cur->next));
  return (s8){.buf = buf, .len = tot};
}

// ──────────────────────────────────────────────────────────── Operating system

// Should these indicate success?
void flush(bufout *b) {
  if (!b->err && b->len) {
      b->err = !oswrite(b->fd, b->buf, b->len);
      b->len = 0;
    }
}

void error(i32 code, s8 msg) {
  oswrite(2, (u8 *)msg.buf, msg.len);
  oswrite(2, (u8 *)"\n", 1);
  osfail(code);
}

void debug(s8 msg) { // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ debug
  oswrite(2, (u8 *)"[", 1);
  oswrite(2, (u8 *)msg.buf, msg.len);
  oswrite(2, (u8 *)"]\n", 2);
}

// have you heard of a debugger!?
void denibbles(byte nib) {
  if (nib < 0xa) oswrite(2, &(u8){nib + '0'}, 1);
  else oswrite(2, &(u8){nib - 0xa + 'a'}, 1);
}

void debytes_impl(void *val, size len) { // too cool for stdio.h printf
  byte *b = (byte *)val;
  oswrite(2, (u8 *)"0x", 2);
  for (size i = len - 1; i >= 0; i--) { // hardcoded little-endian
    denibbles(*(b + i) >> 4 & 0xF); // upper nibble
    denibbles(*(b + i) & 0xF);      // lower nibble
    if (i > 0 && i % 4 == 0 && i % 8 != 0)
      oswrite(2, (u8 *)" ", 1);
    if (i > 0 && i % 8 == 0)
      oswrite(2, (u8 *)"\n  ", 3);
  }
  oswrite(2, (u8 *)"\n", 1);
}

#ifndef _WIN32 // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ not _WIN32

#include <stdlib.h> // plus malloc.h on Windows?
#include <unistd.h>

arena malloc_arena(size cap) {
  arena a = {0}; // zero-initialise
  a.beg = malloc(cap);
  a.end = a.beg ? a.beg + cap : 0;
  return a;
}

void osfail(i32 code) {
  _exit(code); // terminate without cleanup https://stackoverflow.com/a/5423108/780743
}

i32 osread(i32 fd, u8 *buf, i32 cap) {
  return (i32)read(fd, buf, cap);
}

b32 oswrite(i32 fd, u8 *buf, i32 len) {
  for (i32 off = 0; off < len; ) {
    i32 r = (i32)write(fd, buf + off, len - off);
    if (r < 1) return 0;
    off += r;
  }
  return 1;
}

#endif // not _WIN32
