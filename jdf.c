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

u8 *s8find(s8 haystack, s8 needle) {
  u8 *found = 0;
  u8 *end = lastof(haystack);
  // init first, cond before loop, iter after loop
  for (u8 *h = haystack.buf; !found && (h < end); h++) {
    for (u8 *n = needle.buf;
         (n < needle.buf + needle.len) && (h < end);
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

b32 whitespace(u8 c) {
  switch (c) {
  case ' ':
  case '\t':
  case '\n':
  case '\r':
    return 1;
  }
  return 0;
}

s8 s8trim(s8 src) {
  u8 *beg = src.buf;
  u8 *end = lastof(src);
  while (beg < end && whitespace(*beg)) beg++;
  while (end > beg && whitespace(*(end - 1))) end--;
  return s8span(beg, end);
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
  s8 c = (s8) {
    .buf = new (a, u8, s.len),
    .len = s.len
  };
  copy(c.buf, s.buf, s.len);
  return c;
}

s8 s8concat(arena *a, s8 **ss, size len) {
  size tot = 0;
  for (size i = 0; i < len; i++) tot += ss[i]->len;
  u8 *buf = new(a, u8, tot);
  u8 *beg = buf;
  for (size i = 0; i < len; i++) {
    copy(beg, ss[i]->buf, ss[i]->len);
    beg += ss[i]->len;
  }
  return (s8){.buf = buf, .len = tot};
}

s8 s8sconcat(arena *a, s8s *ss) {
  size tot = 0;
  s8s *cur = ss;
  do {
    tot += cur->val->len;
  } while ((cur = cur->next));
  u8 *buf = new(a, u8, tot);
  u8 *beg = buf;
  cur = ss;
  do {
    copy(beg, cur->val->buf, cur->val->len);
    beg += cur->val->len;
  } while ((cur = cur->next));
  return (s8){.buf = buf, .len = tot};
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

// no stdio.h means no printf
void error(i32 code, s8 msg) {
  oswrite(2, (u8 *)msg.buf, msg.len);
  oswrite(2, (u8 *)"\n", 1);
  osfail(code);
}

#ifndef _WIN32 // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ _WIN32

#include <stdlib.h>
#include <unistd.h>

void osfail(i32 code) {
  _exit(code); // reason for not just `exit`?
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
