#include "jdf.h"

void oom(void) {
  static u8 msg[] = "out of memory\n";
  oswrite(2, (u8 *)msg, lengthof(msg));
  osfail();
}

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

// doesn't check arg validity; end-exclusive
s8 s8span(u8 *beg, u8 *end) {
  s8 s = {0};
  s.buf = beg;
  s.len = end - beg;
  return s;
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

s8 s8clone(arena *a, s8 s) {
  s8 c = {0};
  c.buf = new(a, u8, s.len);
  c.len = s.len;
  copy(c.buf, s.buf, s.len);
  return c;
}

void s8write(bufout *b, s8 s) {
  u8 *buf = s.buf;
  u8 *end = s.buf + s.len;
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

// ──────────────────────────────────────────────────────────── Operating system

// Should these indicate success?
void flush(bufout *b) {
  if (!b->err && b->len) {
      b->err = !oswrite(1, b->buf, b->len);
      b->len = 0;
    }
}

#ifndef _WIN32

#include <stdlib.h>
#include <unistd.h>

void osfail(void) {
  _exit(1);
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

#endif // _WIN32
