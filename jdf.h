/*
  After Wellons https://nullprogram.com/blog/2023/10/08/
  and https://nullprogram.com/blog/2023/09/27/ .
  See discussion https://old.reddit.com/r/C_Programming/comments/173e0vn/nullprogram_my_personal_c_coding_style_as_of_late/ .
  
  - generally omit const (controversial!)
  - literal 0 for null pointers and false
  - restrict when necessary
  - typedef all structures
  - static all functions except for entry points (not applied here; less meaningful in single translation unit build)
  - structure returns instead of out parameters; initialise with {0} as per C99
*/

#ifndef jdf_h
#define jdf_h

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h> // mainly vsnprintf
#include <string.h> // just strlen eww
#include <stdatomic.h>

typedef uint8_t   u8;
#ifdef _WIN32
#include <uchar.h>
typedef char16_t  c16;
#endif
typedef int32_t   b32; // 0 false, 1 true
typedef int32_t   i32;
typedef int64_t   i64;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef float     f32;
typedef double    f64;
typedef uintptr_t uptr;
typedef char      byte;
typedef ptrdiff_t size;
typedef size_t    usize;

typedef struct arena arena; // forward decl

#define alignof(x) (size)_Alignof(x) // casting from size_t
#define countof(arrayptr) (size)(sizeof(arrayptr) / sizeof(*(arrayptr))) // casting from size_t
/*
  Somewhat evil semantic affordance for structs starting with (possibly nested)
  nullable pointer. Allows if(s.ok) process(s.v). Should be safer than null
  pointer because of explicit types. Type name followed by underscore.
  Use to represent e.g. internal allocation failure.
  (Can only cast scalars unfortunately.)
  https://stackoverflow.com/a/3995987/780743
*/
#define MAYBE(t) typedef union {uptr ok; t v;} t##_;
#define new(a, t, n) (t *)alloc(a, sizeof(t), alignof(t), n, #t) // arena, type, number
#define ARRAY(tn, t) /* new type name, el type */            \
  typedef struct {                                           \
    t *buf;                                                  \
    size len;                                                \
  } tn;                                                      \
  MAYBE(tn)                                                  \
  tn##_ make_##tn(arena *a, size len) {                      \
    t *buf = new (a, t, len);                                \
    if (buf) return (tn##_){.v = {.buf = buf, .len = len }}; \
    else return (tn##_){0};                                  \
  }
#define endof(v) ((v).buf + (v).len) // one beyond last of sized value
/*
  To enable assertions in release builds,
  put UBSan in trap mode with -fsanitize-trap
  and then enable at least -fsanitize=unreachable.

  FIXME would be better with error message...
*/
#define assert(c) while (!(c)) __builtin_unreachable()

// ──────────────────────────────────────────────────────────────── Linked lists

typedef struct node_t node_t; struct node_t { node_t *next; }; // ignore subsequent fields
// No loop detection!
size countfn(node_t *node) {
  size c = 0;
  node_t *cur = node;
  if (!cur) return 0;
  do { c++; } while ((cur = cur->next));
  return c;
}
// Indirections to allow use from multiple linked list-derived data structures...
#define count(n) countfn((node_t *)n)
node_t *next(node_t *node) { return node->next; }
node_t *nth(node_t *node, size n) {
  node_t *ret = node;
  for (size i = 0; i < n; i++) {
    if (!ret) return 0;
    ret = ret->next;
  }
  return ret;
}
// Connect two nodes. Can cause loop! Returns any previous `from` tail.
node_t *extend(node_t *from, node_t *to) {
  if (!from) return 0;
  from->next = to;
  return from->next;
}
// Insert up to `count` nodes from `from` after `after`, returning any remaining `from` tail.
node_t *insert(node_t *after, node_t *from, size count) {
  if (!after || !from || count <= 0) return from;
  node_t *after_tail = after->next;
  node_t *to = nth(from, count - 1);
  node_t *to_tail = to ? to->next : 0;
  after->next = from;
  to->next = after_tail;
  return to_tail;
}
/*
  Define new linked list type tn, el type t.
  t can be typename * for pointer (i.e. reference list).

  <tn>append appends node with value `m` to node `maybe`.
  If `maybe` doesn't exist, append starts a new list.
  If `maybe` already has a ->next, append redirects it, orphaning tail unless
caller retains it. Caller needs to retain list head.
  Does not prevent inclusion of stack-allocated values in heap-allocated list!
*/
#define LIST(tn, t)                                                            \
  typedef struct tn tn;                                                        \
  struct tn {                                                                  \
    tn *next;                                                                  \
    t val;                                                                     \
  };                                                                           \
  tn *tn##append(arena *a, tn *maybe, t m) {                                   \
    tn *cur = new (a, tn, 1);                                                  \
    if (!cur) return 0;                                                        \
    cur->val = m;                                                              \
    if (maybe)                                                                 \
      maybe->next = cur;                                                       \
    return cur;                                                                \
  }                                                                            \
  tn *tn##next(tn *node) { return (tn *)next((node_t *)node); }                \
  tn *tn##nth(tn *node, size n) { return (tn *)nth((node_t *)node, n); }       \
  tn *tn##extend(tn *from, tn *to) {                                           \
    return (tn *)extend((node_t *)from, (node_t *)to);                         \
  }                                                                            \
  tn *tn##insert(tn *after, tn *from, size n) {                                \
    return (tn *)insert((node_t *)after, (node_t *)from, n);                   \
  }

/*
  Define new association list type with ...assoc, ...dissoc, ...get.
  kt and vt can be typename * for pointer, caller provides appropriate keq fn.
  Does not check that head is actually head!
  Does not prevent inclusion of stack-allocated kvs in heap-allocated list!
  Make sure to use `dissoc`s returned head! Dissoc final key will return null.
  Makes no attempt to compact or reorder storage within arena.
*/
#define MAP_LIST(tn, kt, vt, keq)                                     \
  typedef struct tn tn;                                               \
  struct tn {                                                         \
    tn *next;                                                         \
    kt key;                                                           \
    vt val;                                                           \
  };                                                                  \
  /* Uniquely associate key to value. Caller must ensure kv validity. \
     Assoc to null head to make new association list.                 \
     Allows null val. Returns null pointer if new fails. */           \
  tn *tn##assoc(arena *a, tn *head, kt key, vt val) {                 \
    tn *beg = 0;                                                      \
    if (!head) {                                                      \
      beg = new (a, tn, 1);                                           \
      if (!beg)                                                       \
        return 0;                                                     \
      beg->key = key;                                                 \
      beg->val = val;                                                 \
      return beg;                                                     \
    }                                                                 \
    beg = head;                                                       \
    tn *cur = beg;                                                    \
    tn *prev = 0;                                                     \
    for (; cur; prev = cur, cur = cur->next) {                        \
      if (keq(cur->key, key)) {                                       \
        cur->val = val;                                               \
        return beg;                                                   \
      }                                                               \
    }                                                                 \
    cur = prev->next = new (a, tn, 1);                                \
    if (!cur)                                                         \
      return 0;                                                       \
    cur->key = key;                                                   \
    cur->val = val;                                                   \
    return beg;                                                       \
  }                                                                   \
  tn *tn##dissoc(tn *head, kt key) {                                  \
    if (!head)                                                        \
      return 0;                                                       \
    tn *cur = head;                                                   \
    tn *prev = 0;                                                     \
    for (; cur; prev = cur, cur = cur->next) {                        \
      if (keq(cur->key, key)) {                                       \
        if (prev) prev->next = cur->next;                             \
        else return cur->next;                                        \
      }                                                               \
    }                                                                 \
    return head;                                                      \
  }                                                                   \
  /* Return possibly-null pointer to kv pair with key match. */       \
  tn *tn##get(tn *head, kt key) {                                     \
      if (!head)                                                      \
        return 0;                                                     \
      tn *cur = head;                                                 \
    do {                                                              \
      if (keq(cur->key, key)) return cur;                             \
    } while ((cur = cur->next));                                      \
    return 0;                                                         \
  }
// Barely worth it vs ASSOCIATION_LIST with ignored vt. Make sure to use `disj`s returned head!
#define SET_LIST(tn, kt, keq)                   \
  typedef struct tn tn;                         \
  struct tn {                                   \
    tn *next;                                   \
    kt key;                                     \
  };                                            \
  tn *tn##conj(arena *a, tn *head, kt key) {    \
    tn *beg = {0};                              \
    if (!head) {                                \
      beg = new (a, tn, 1);                     \
      if (!beg)                                 \
        return 0;                               \
      beg->key = key;                           \
      return beg;                               \
    }                                           \
    beg = head;                                 \
    tn *cur = beg;                              \
    tn *prev = 0;                               \
    for (; cur; prev = cur, cur = cur->next)    \
      if (keq(cur->key, key))                   \
        return beg;                             \
    cur = new (a, tn, 1);                       \
    if (!cur)                                   \
      return 0;                                 \
    prev->next = cur;                           \
    cur->key = key;                             \
    return beg;                                 \
  }                                             \
  tn *tn##disj(tn *head, kt key) {              \
    if (!head)                                  \
      return 0;                                 \
    tn *cur = head;                             \
    tn *prev = 0;                               \
    for (; cur; prev = cur, cur = cur->next) {  \
      if (keq(cur->key, key)) {                 \
        if (prev)                               \
          prev->next = cur->next;               \
        else return cur->next;                  \
      }                                         \
    }                                           \
    return head;                                \
  }                                             \
  tn *tn##has(tn *head, kt key) {               \
    if (!head)                                  \
      return 0;                                 \
    tn *cur = head;                             \
    do {                                        \
      if (keq(cur->key, key))                   \
        return cur;                             \
    } while ((cur =  cur->next));               \
    return 0;                                   \
  }

// ─────────────────────────────────────────────────────────────────────── Arena

/*
  Pass "store" arena by reference, and "scratch" by value.
  This effectively resets the scratch *cur pointer on fn return.
*/
typedef struct arena {
  // https://stackoverflow.com/a/21476937/780743
  // easier not to have `byte *const beg` and end to facilitate free_arena
  byte *beg; // original start of arena
  byte *cur; // cursor: current start of free space
  byte *end; // allocated end of arena
} arena;

// TODO could visualise correctness of padding algorithm
/*
  Allocate space within arena. Use via `new` macro.
  Not designed to be threadsafe! Each thread requires its own arena/s.
  NB It's somewhat redundant to test for failure of alloc_arena, because the
  first alloc here would fail if the arena is 0.
*/
byte *alloc(arena *a, size objsize, size align, size count, const char *t) {
  if (!a || count <= 0 || align < 0) return 0; // why are count and size signed?
  //printf("Trying to allocate %ti %ss of size %ti\n", count, t, objsize);
  size avail = a->end - a->cur;
  /*
    Padding is how far the next aligned address is beyond the cursor.
   
    Use wrapping unsigned integer negation of the cursor address to measure what's left rather than what's in use.
    Calculate how far this address is beyond the previous aligned address using modulo:
   
     addr % align == addr & (align - 1) // because align is a power of 2 (i.e. > 0)

     Example with u4 address and 4 byte alignment:
          0x  0   4   8   c   
         cur  ---------->..... 0xb 0b1011
        -cur  .....<---------- 0x5 0b0101
       align  |   |   |   |    4   0b0100
     align-1                       0b0011
     padding      x            1   0b0001 == -cur & (align-1)
      giving  ----------->.... 0xc 0b1100
   */
  size padding = -(uptr)a->cur & (align - 1);
  /*
    Deliberately return null pointer if arena can't allocate requested amount!
    This does propagate annoyingly.
    Distinction between OOM proper and getting killed by (Linux) OOM killer?
  */
  if (count > (avail - padding) / objsize) {
    fprintf(stderr, "Couldn't allocate %s: count %ti, count available %ti\n", t, count, (avail-padding)/objsize);
    return 0;
  }
  size total = count * objsize;
  byte *p = a->cur + padding;
  a->cur += padding + total;
  for (size i = 0; i < total; i++) p[i] = 0;
  return p;
}

size capacity(arena *a) { return a->end - a->beg; }
size used(arena *a) { return a->cur - a->beg; }
size available(arena *a) { return a->end - a->cur; }

size KiB(u32 n) { return (1<<10) * n; }
size MiB(u32 n) { return (1<<20) * n; }

// Caller to check for null pointers, which fail silently.
// Linker error when attempted `inline`...
size copy(u8 *restrict dst, u8 *restrict src, size len) {
  if (!(dst && src)) return 0;
  for (size i = 0; i < len; i++) dst[i] = src[i];
  return len;
}

// ───────────────────────────────────────────────────────────────────── Strings

ARRAY(s8, u8) // s8: Basic UTF-8 string. Not null terminated!
// Wrap C string literal into s8 string.
#define s8(s) (s8){(u8 *)(s), countof(s) - 1}
static const s8_ s8_OOM = {.v = s8("error: out of memory")};
ARRAY(s8a, s8)
#ifdef _WIN32
ARRAY(s16, c16)
#define s16(s) (s16) { (c16 *)(s), countof(s) - 1 }
// TODO what about all the fns?!
#endif

LIST(s8l, s8)
/*
  Make one s8 from unquoted multiline text, after collapsing whitespace.
  IDE may be annoying about it, try fundamental-mode.
  Compare with s8("first line"⏎"second line"⏎"etc").
*/ 
#define text(...) s8(#__VA_ARGS__) // https://stackoverflow.com/a/17996915/780743

/*
  Slice using pointers, doesn't check that actually within an s8!
  Not MAYBE because doesn't allocate.
*/
s8 s8span(u8 *beg, u8 *end) {
  if (beg && end >= beg) return (s8){.buf = beg, .len = end - beg};
  return (s8){0};
}

s8 s8bytespan(byte *beg, byte *end) {
  return s8span((u8 *)beg, (u8 *)end);
}

// Slice forward using clamped offsets, which may be positive or negative (i.e. from start or end, respectively)
s8 s8slice(s8 src, size from, size to) {
  s8 s = {.buf = src.buf};
  size f = (from < 0) ? src.len + from : from;
  size t = (to > 0) ? to : src.len + to;
  if (f < 0) f = 0; // clamp offsets
  if (t > src.len) t = src.len;
  s.buf += f;
  if (t > f) s.len = t - f;
  else s.len = 0; // refuse to slice backwards
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

// Why `size`? TODO visualise hashification of input
size s8hash(s8 s) {
  u64 h = 0x100;
  for (size i = 0; i < s.len; i++) {
    h ^= s.buf[i];
    h *= 1111111111111111111u; // nineteen ones
  }
  return (h ^ h>>32) & (u32)-1;
}

// Find string
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
        if (found) h = found; // will then be incremented by outer loop
        found = 0;
        break;
      }
    }
  }
  return found;
}

// Find char
u8 *s8findu8(s8 haystack, u8 needle) {
  if (!haystack.buf) return 0; // allow \0 needle
  u8 *end = endof(haystack);
  for (u8 *h = haystack.buf; h < end; h++)
    if (*h == needle)
      return h;
  return 0;
}

b32 s8startswith(s8 s, s8 with) {
  return s8equal(s8slice(s, 0, with.len), with);
}

b32 s8endswith(s8 s, s8 with) {
  return s8equal(s8slice(s, -with.len, 0), with);
}

// Wrap decayed C string into s8 string
s8 s8wrap(const char *cstr, size maxlen) {
  if (!cstr) return (s8){0};
  u8 *beg = (u8 *)cstr;
  u8 *end = beg;
  while (*end != '\0' && (end-beg) < maxlen) end++;
  return s8span(beg, end);
}

// Return pointer to copy of s in a, one byte longer for terminal
// zero. Null if allocation fails. May be simpler to do manually with
// stack-allocated buffer for known-short strings.
char *s8unwrap(arena *a, s8 s) {
  u8 *buf = new (a, u8, s.len + 1); // is zeroed
  if (!buf) return 0;
  copy(buf, s.buf, s.len);
  return (char *)buf;
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
  while (end > beg && whitespace(*(end - 1))) end--;
  while (beg < end && whitespace(*beg)) beg++;
  return s8span(beg, end);
}

/* Is s only whitespace? */
b32 s8blank(s8 s) {
  for (size i = 0; i < s.len; i++)
    if (!whitespace(s.buf[i]))
      return 0;
  return 1;
}

// Copies buf, optionally null-terminated for easier interop.
s8_ s8clone(arena *a, s8 s, b32 null_terminate) {
  s8_ c = make_s8(a, null_terminate ? s.len + 1 : s.len);
  if (!c.ok) return c;
  copy(c.v.buf, s.buf, s.len);
  return c;
}

typedef struct {
  s8 head;
  s8 tail;
  b32 ok; // different to MAYBE because head could be empty
} s8pair;

s8pair s8cut(s8 s, s8 on) {
  u8 *found = s8find(s, on);
  if (!found) return (s8pair){0};
  return (s8pair) {.head = s8span(s.buf, found), .tail = s8span(found + on.len, endof(s)), .ok = 1};
}

s8pair s8cutu8(s8 s, u8 on) {
  u8 *found = s8findu8(s, on);
  if (!found) return (s8pair){0};
  return (s8pair) {.head = s8span(s.buf, found), .tail = s8span(found + 1, endof(s)), .ok = 1};
}

/*
  Split s, returning s8spans referring to it (zero copy of buffer).
  max_splits can be 0 for unlimited splits.
*/
s8a_ s8split(arena *store, arena scratch, s8 s, s8 on, size max_splits) {
  if (s.len == 0) return (s8a_){.v = {.buf = (s8 *)s.buf, .len = 0}}; // Hack to show ok-ness.
  u8 *end = endof(s);
  u8 **matches = (u8 **)scratch.beg;
  size match_count = 0;
  for (u8 *cur = s.buf; cur < end && (max_splits == 0 || match_count < max_splits);) {
    cur = s8find(s8span(cur, end), on);
    if (!cur) break;
    u8 **match = new (&scratch, u8 *, 1); 
    if (!match) return (s8a_){0}; // FIXME 2025-08-10 21:53:36 clearer indication of alloc fail
    matches[match_count++] = cur;
    cur += on.len;
  }
  s8 *buf = new (store, s8, match_count + 1);
  if (!buf) return (s8a_){0};
  for (size i = 0; i <= match_count; i++) 
    buf[i] = s8span(i == 0 ? s.buf : (matches[i - 1] + on.len),
                    i == match_count ? end : matches[i]);
  return (s8a_){.v = {.buf = buf, .len = match_count + 1}};
}

// Trivially less efficient than impl calling s8findu8, but easier to maintain.
s8a_ s8splitu8(arena *store, arena scratch, s8 s, u8 on, size max_splits) {
  s8 ons = (s8){.buf = (u8[]){on}, .len = 1};
  return s8split(store, scratch, s, ons, max_splits);
}

s8_ s8replace(arena *store, arena scratch,
              s8 source, s8 target, s8 replacement) {
  s8a_ split = s8split(store, scratch, source, target, 0);
  if (!split.ok) return (s8_){0};
  if (split.v.len == 1) return (s8_) {.v = source};
  size len = (split.v.len - 1) * replacement.len;
  for (size i = 0; i < split.v.len; i++) len += split.v.buf[i].len;
  s8_ ret = make_s8(store, len);
  if (!ret.ok) return (s8_){0};
  copy(ret.v.buf, split.v.buf[0].buf, split.v.buf[0].len);
  u8 *cur = ret.v.buf + split.v.buf[0].len;
  for (size i = 1; i < split.v.len; i++) {
    copy(cur, replacement.buf, replacement.len);
    cur += replacement.len;
    copy(cur, split.v.buf[i].buf, split.v.buf[i].len);
    cur += split.v.buf[i].len;
  }
  return ret;
}

// Concatenate array of strings
s8_ s8concat(arena *a, s8 *ss, size len) {
  size tot = 0;
  for (size i = 0; i < len; i++) tot += ss[i].len;
  s8_ ret = make_s8(a, tot);
  if (!ret.ok) return ret;
  u8 *cur = ret.v.buf;
  for (size i = 0; i < len; i++) {
    copy(cur, ss[i].buf, ss[i].len);
    cur += ss[i].len;
  }
  return ret;
}

size s8llen(s8l *sl) {
  size len = 0;
  s8l *node = sl;
  do { len += node->val.len; } while ((node = node->next));
  return len;
}
     
s8_ s8lconcat(arena *store, s8l *sl) {
  s8_ ret = make_s8(store, s8llen(sl));
  if (!ret.ok) return ret;
  u8 *cur = ret.v.buf;
  s8l *node = sl;
  do {
    copy(cur, sl->val.buf, sl->val.len);
    cur += sl->val.len;
  } while ((node = node->next));
  return ret;
}

s8_ u8fill(arena *buf, u8 with, size count) {
  s8_ ret = make_s8(buf, count);
  if (!ret.ok) return ret;
  for (size i = 0; i < count; i++) ret.v.buf[i] = with;
  return ret;
}

// Does effectively allocate by moving buf.cur, so MAYBE return type.
// Also see s8printf.
s8_ s8sprintf(arena *buf, const char *format, ...) {
  if (!buf || !buf->cur) return (s8_){0};
  byte *start = buf->cur;
  size avail = available(buf);
  va_list args;
  va_start(args, format);
  // returns misleading n which disregards available size!
  // also disregards terminal \0, as usual
  // would drop a character if avail didn't have room for \0
  i32 n = vsnprintf(start, avail, format, args);
  va_end(args);
  if (n > 0) {
    buf->cur += n > avail ? avail : n;
    return (s8_){.v = s8bytespan(start, buf->cur)};
  } else return (s8_){0};
}

// ───────────────────────────────────────────────── Lock-free concurrent queues
// https://nullprogram.com/blog/2022/05/14
typedef _Atomic u32 queue; // typedef _Atomic ... is ok as per stdatomic.h
// len must be positive, <= 32768, and a power of two.
// NB Actual storage must be size cap + 1! ▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚▚
i32 queue_capacity(i32 len) {
  if ((len <= 0) || (len > 1 << 16) || (len & (len - 1))) {
    fprintf(stderr, "Invalid queue storage length: %d", len);
    return 0; 
  }
  return len - 1;
}
// ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Multiple consumer
i32 queue_mpop(queue *q, i32 len, u32 *save) {
  u32 r = *save = *q;
  i32 mask = len - 1;
  i32 head = r       & mask;
  i32 tail = r >> 16 & mask;
  return head == tail ? -1 : tail;
}
// NB element load must be atomic
b32 queue_mpop_commit(queue *q, u32 save) {
  return atomic_compare_exchange_strong(q, &save, save + 0x10000);
}
// ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Single consumer
// Returns index for next value to be popped. -1 when empty.
i32 queue_pop(queue *q, i32 len) {
  // printf("queue_pop %p 0x%x %i\n", q, *q, len);
  u32 r = *q; // ? memory_order_acquire from stdatomic.h
  i32 mask = len - 1;
  i32 head = r       & mask;
  i32 tail = r >> 16 & mask;
  return head == tail ? -1 : tail;
}
void queue_pop_commit(queue *q) {
  *q += 0x10000; // 0x10000 == 1u << 16 i.e. increment tail ; ? memory_order_release
}
// ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Multiple producer
// TODO 2025-10-02 03:08:56 test, analyse
i32 queue_mpush(queue *q, i32 len, u32 *save) {
  u32 r = *save = *q;
  i32 mask = len - 1;
  i32 head = r       & mask;
  i32 tail = r >> 16 & mask;
  i32 next = (head + 1u) & mask;
  if (r & 0x8000) *q &= ~0x8000;
  return next == tail ? -1 : head;
}
// Presumably element store must be atomic
b32 queue_mpush_commit(queue *q, u32 save) {
  return atomic_compare_exchange_strong(q, &save, save + 1);
} 
// ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴  Single producer
// Returns index for next value to be pushed. -1 when full.
i32 queue_push(queue *q, i32 len) {
  //printf("queue_push %p 0x%x %i\n", (void *)q, *q, len);
  u32 r = *q;
  i32 mask = len - 1;
  i32 head = r       & mask;
  i32 tail = r >> 16 & mask;
  i32 next = (head + 1u) & mask;
  // 0x8000 == 1 << 15; ~0x8000 is zero at bit 15, rest ones.
  // printf("mask 0x%x, head 0x%x, tail 0x%x, next 0x%x\n", mask, head, tail, next); 
  if (r & 0x8000) *q &= ~0x8000;  // avoid overflow (of head into tail bytes) on commit
  return next == tail ? -1 : head;
}
// After storing into (separately allocated) element array.
void queue_push_commit(queue *q) {
  *q += 1;
}
// ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Concurrent output buffer
typedef struct {
  s8 buf;
  queue q;
} qout;
MAYBE(qout)
qout_ make_qout(arena *a, i32 len) {
  qout_ nil = (qout_){0};
  i32 cap = queue_capacity(len);
  if (!cap) return nil;
  u8 *buf = new (a, u8, len);
  if (!buf) return nil;
  return (qout_) { .v = {.buf = (s8){.buf = buf, .len = len}, .q = 0 } };
}
size read_qout(qout *qo, s8 buf) {
  i32 qi = 0;
  size bi = 0;
  while ((qi = queue_pop(&qo->q, qo->buf.len))) { // where .len is queue capacity
    if (qi < 0) break; // empty
    buf.buf[bi++] = qo->buf.buf[qi];
    queue_pop_commit(&qo->q);
  }
  return bi;
}
size write_qout(qout *qo, u8 *buf, size maxlen) {
  i32 qi = 0;
  i32 bi = 0;
  while (1) {
    qi = queue_push(&qo->q, qo->buf.len); // where .len is queue capacity
    if (qi < 0 || bi >= maxlen) break;
    // printf("pushing %c to queue position %i\n", buf[bi], qi);
    // printf("%c", buf[bi]);
    qo->buf.buf[qi] = buf[bi++]; // byte at a time
    queue_push_commit(&qo->q);
  }
  return bi;
}

// ────────────────────────────────────────────────────────────────────── Output

typedef struct {
  u8 *buf; // buffer itself, e.g. allocated with `new` macro
  size len; // current length of buffer contents
  size cap; // capacity of buffer, set at initialisation
  i32 fd; // 1 stdout, 2 stderr
  b32 err;
} bufout;
MAYBE(bufout)
bufout_ make_bufout(arena *a, i32 cap, i32 fd) {
  u8 *buf = new (a, u8, cap);
  if (!buf) return (bufout_){0};
  return (bufout_){.v = {.buf = buf, .cap = cap, .fd = fd}};
}

void flush(bufout *b);

typedef size (*Writer)(void *out, s8 s);

// Caller needs to flush
size s8write(void *out, s8 s) {
  bufout *b = (bufout *)out;
  if (!b->buf || !s.buf) return 0;
  u8 *buf = s.buf;
  u8 *end = endof(s);
  size total_copied = 0;
  while (!b->err && (buf < end)) {
    i32 avail = b->cap - b->len; // TODO learn about size -> i32
    i32 count = (avail < end - buf) ? avail : (i32)(end - buf);
    // TODO learn benefit of copying rather than just stepping through s
    copy(b->buf + b->len, buf, count);
    buf += count;
    b->len += count;
    total_copied += count;
    if (b->len == b->cap) flush(b);
  }
  return total_copied;
}

// Also see s8sprintf.
i32 s8printf(arena scratch, Writer writer, void *out, const char *format, ...) {
  if (!scratch.beg) return -1;
  assert(scratch.beg == scratch.cur);
  size avail = available(&scratch);
  va_list args;
  va_start(args, format);
  // returns misleading n which disregards available size!
  // also disregards terminal \0, as usual
  i32 n = vsnprintf(scratch.beg, avail, format, args);
  va_end(args);
  if (n > 0) {
    scratch.cur += (n > avail ? avail : n); // at terminal \0
    return writer(out, s8bytespan(scratch.beg, scratch.cur));
  } else return n;
}

u32 oswrite(i32 fd, u8 *buf, i32 len);

// Should these indicate success?
void flush(bufout *b) {
  if (!b->err && b->len) {
      b->err = oswrite(b->fd, b->buf, b->len);
      b->len = 0;
    }
}

void s8writefd(i32 fd, s8 s) {
  oswrite(fd, (u8 *)s.buf, s.len);
}

// Unbuffered
void s8log(i32 fd, s8 s) {
  s8writefd(fd, s);
  s8writefd(fd, s8("\n"));
}

// ──────────────────────────────────────────────────────────── Operating System

void osfail(i32 code);

void failwith(i32 code, s8 msg) {
  s8log(2, msg);
  osfail(code);
}

// ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ debug

// have you heard of a debugger!?
void denibble(i32 fd, byte nib) {
  if (nib < 0xa) oswrite(fd, &(u8){nib + '0'}, 1);
  else oswrite(fd, &(u8){nib - 0xa + 'a'}, 1);
}

// Silly portmanteau
void debytes(i32 fd, void *val, size len) { // too cool for stdio.h printf
  byte *b = (byte *)val;
  oswrite(fd, (u8 *)"0x", 2);
  for (size i = len - 1; i >= 0; i--) { // hardcoded little-endian
    denibble(fd, *(b + i) >> 4 & 0xF); // upper nibble
    denibble(fd, *(b + i) & 0xF);      // lower nibble
    if (i > 0 && i % 4 == 0 && i % 8 != 0)
      oswrite(fd, (u8 *)" ", 1);
    if (i > 0 && i % 8 == 0)
      oswrite(fd, (u8 *)"\n  ", 3);
  }
  oswrite(fd, (u8 *)"\n", 1);
}

#define inspect(ptr)                                  \
  s8write_(2, s8("Contents of pointer " #ptr ":"));   \
  debytes(2, ptr, sizeof(*(ptr)))

#define log_debug(s)                              \
  s8writefd(2, s8("Value of " #s ": "));          \
  s8log(2, s)

// TODO why are there so many signed ints below where negative is incorrect?

#ifdef _WIN32 // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ _WIN32
// msys2 clang64 to get asan & usan
// /usr/share/mintty/emojis/getemojis -d
typedef struct { i32 dummy; } *handle;
#define W32(r) __declspec(dllimport) r __stdcall
W32(byte *) VirtualAlloc(byte *, usize, u32, u32);
W32(b32) VirtualFree(byte *, usize, u32);
W32(handle) GetStdHandle(u32);
W32(b32) ReadFile(handle, u8 *, u32, u32 *, void *);
W32(b32) WriteFile(handle, u8 *, u32, u32 *, void *);
W32(void) ExitProcess(u32);
W32(u32) GetLastError(void);

arena alloc_arena(size cap) {
  // https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc
  // lpAddress = system determines where to allocate
  // dwSize = size in bytes
  // flAllocationType =  MEM_COMMIT + MEM_RESERVE
  // flProtect = PAGE_READWRITE
  byte* beg = VirtualAlloc(0, cap, 0x3000, 4);
  byte* end = beg ? beg + cap : 0;
  if (beg) return (arena){.beg = beg, .cur = beg, .end = end};
  else return (arena){0};
}

b32 free_arena(arena *a) {
  b32 ret = 0;
  if (!a) return 0;
  byte *me = a->beg;
  a->beg = 0;
  a->cur = 0;
  a->end = 0;
  if (me) ret = VirtualFree(me, 0, 0x00008000); // MEM_RELEASE
  return ret; // nonzero on success
}

void osfail(i32 code) {
  ExitProcess(code); // TOOD check vs 1
}

i32 osread(i32 fd, u8 *buf, i32 cap) {
  handle in = GetStdHandle(-10 - fd);
  u32 len;
  ReadFile(in, buf, cap, &len, 0);
  return len;
}

u32 oswrite(i32 fd, u8 *buf, i32 len) {
  handle out = GetStdHandle(-10 - fd);
  u32 dummy;
  b32 stat = WriteFile(out, buf, len, &dummy, 0); // TODO GetLastError if fails
  if (stat) return 0;
  else return GetLastError();
}

// https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess
// void mainCRTStartup(void) { stuff; ExitProcess(code); }
        
#else // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ not _WIN32
 
#include <stdlib.h> // malloc
#include <unistd.h> // read write _exit
#include <errno.h>

// malloc failure will return zero-capacity arena so its `alloc`s will just fail.
arena alloc_arena(size cap) {
  byte* beg = malloc(cap);
  byte* end = beg ? beg + cap : 0;
  if (beg) return (arena){.beg = beg, .cur = beg, .end = end};
  else return (arena){0};
}

b32 free_arena(arena *a) {
  if(!a) return 0;
  byte *me = a->beg;
  a->beg = 0;
  a->cur = 0;
  a->end = 0;
  free(me); // safe even if null
  return 1;
}

void osfail(i32 code) {
  _exit(code); // terminate without cleanup https://stackoverflow.com/a/5423108/780743
}

i32 osread(i32 fd, u8 *buf, i32 cap) {
  return (i32)read(fd, buf, cap);
}

u32 oswrite(i32 fd, u8 *buf, i32 len) {
  for (i32 off = 0; off < len; ) {
    i32 r = (i32)write(fd, buf + off, len - off);
    if (r < 1) return errno;
    off += r;
  }
  return 0;
}

// i32 main(void) { stuff; return r; }

#endif // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴

#endif // jdf_h
