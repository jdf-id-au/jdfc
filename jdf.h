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
#include <stdio.h> // just for vsnprintf

typedef uint8_t   u8;
#ifdef _WIN32
typedef char16_t  c16;
#endif
typedef int32_t   b32; // 0 false, 1 true
typedef int32_t   i32;
typedef int64_t   i64;
typedef uint32_t  u32;
typedef uint64_t u64;
typedef float     f32;
typedef double    f64;
typedef uintptr_t uptr;
typedef char      byte;
typedef ptrdiff_t size;
typedef size_t    usize;

/*
  Pass "store" arena by reference, and "scratch" by value.
  This effectively resets the scratch *cur pointer on fn return.
*/
typedef struct {
  // https://stackoverflow.com/a/21476937/780743
  // easier not to have `byte *const beg` and end to facilitate free_arena
  byte *beg; // original start of arena
  byte *cur; // cursor: current start of free space
  byte *end; // allocated end of arena
} arena;

#define alignof(x) (size)_Alignof(x) // casting from size_t
#define countof(arrayptr) (size)(sizeof(arrayptr) / sizeof(*(arrayptr))) // casting from size_t
#define new(a, t, n) (t *)alloc(a, sizeof(t), alignof(t), n) // arena, type, number
#define ARRAY(tn, t) typedef struct { t *buf; size len; } tn; // new type name, el type
#define endof(v) (v).buf + (v).len // one beyond last of sized value
/*
  Somewhat evil semantic affordance for structs starting with (possibly nested) nullable pointer.
  Allows if(s.ok) process(s.v). Use to represent e.g. internal allocation failure.
  (Can only cast scalars unfortunately.)
  https://stackoverflow.com/a/3995987/780743
*/
#define MAYBE(t) typedef union { uptr ok; t v; } t##_;
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
  if (!cur)
    return 0;
  do { c++; } while ((cur = cur->next));
  return c;
}
#define count(n) countfn((node_t *)n)
// Indirection to allow use from multiple linked list-derived data structures...
node_t *next(node_t *node) { return node->next; }
node_t *nth(node_t *node, size n) {
  node_t *ret = node;
  for (size i = 0; i < n; i++) ret = ret->next;
  return ret;
}
// Connect two nodes. Can cause loop!
node_t *extend(node_t *from, node_t *to) {
  if (!from) return 0;
  from->next = to;
  return to;
}
/*
  Connect `after` to `from`, and `to` to `after`s tail.
  Returns `to`s tail. Does not check `to` follows `from`.
  (This could be used to exchange tails...)
 */
node_t *insert(node_t *after, node_t *from, node_t *to) {
  node_t *next = after->next;
  node_t *tail = to->next;
  after->next = from;
  to->next = next;
  return tail;
}
// NB impl of `last` would need loop detector
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
  tn *tn##insert(tn *after, tn *from, tn *to) {                                \
    return (tn *)insert((node_t *)after, (node_t *)from, (node_t *)to);        \
  }

/*
  Define new association list type with ...count, ...assoc, ...dissoc, ...get.
  kt and vt can be typename * for pointer, caller provides appropriate keq fn.
  Does not check that head is actually head!
  Does not prevent inclusion of stack-allocated kvs in heap-allocated list!
  Make sure to use returned head! Dissoc final key will return null.
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
   Returns null pointer if new fails. */                              \
  tn *tn##assoc(arena *a, tn *head, kt key, vt val) {                 \
    tn *beg = {0};                                                    \
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
        return cur->next;                                             \
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
// Barely worth it vs ASSOCIATION_LIST with ignored vt. Make sure to use `disj`s returnd head!
#define SET_LIST(tn, kt, keq)                   \
  RPTR(tn)                                      \
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
        return cur->next;                       \
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

// TODO could visualise correctness of padding algorithm
// Allocate space within arena. Use via `new` macro.
byte *alloc(arena *a, size objsize, size align, size count) {
  if (!a || count <= 0 || align < 0) return 0; // why are count and size signed?
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
  if (count > (avail - padding)/objsize) return 0;
  size total = count * objsize;
  byte *p = a->cur + padding;
  a->cur += padding + total;
  for (size i = 0; i < total; i++) p[i] = 0;
  return p;
}

size capacity(arena *a) { return a->end - a->beg; }
size used(arena *a) { return a->cur - a->beg; }
size remaining(arena *a) { return a->end - a->cur; }

size KiB(u32 n) { return (1<<10) * n; }
size MiB(u32 n) { return (1<<20) * n; }

// Caller to check for null pointers, which fail silently.
void copy(u8 *restrict dst, u8 *restrict src, size len) {
  if (dst && src) for (size i = 0; i < len; i++) dst[i] = src[i];
}

// ───────────────────────────────────────────────────────────────────── Strings

ARRAY(s8, u8) // s8: Basic UTF-8 string. Not null terminated!
// Wrap C string literal into s8 string.
#define s8(s) (s8) { (u8 *)s, countof(s) - 1 }
MAYBE(s8)
ARRAY(s8s, s8)
MAYBE(s8s)     
#ifdef _WIN32
ARRAY(s16, c16)
#define s16(s) (s16) { (c16 *)s, countof(s) - 1 }
// TODO what about all the fns?!
#endif

/*
  Make one s8 from unquoted multiline text, after collapsing whitespace.
  IDE may be annoying about it, try fundamental-mode.
  Compare with s8("first line"⏎"second line"⏎"etc").
*/ 
#define text(...) s8(#__VA_ARGS__) // https://stackoverflow.com/a/17996915/780743

// Slice using pointers, doesn't check that actually within an s8!
s8 s8span(u8 *beg, u8 *end) {
  if (beg && end && end >= beg) return (s8){.buf = beg, .len = end - beg};
  return (s8){0};
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

// Why `size`?
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

// Return copy of s into a, one byte longer for terminal zero.
s8_ s8unwrap(arena *a, s8 s) {
  u8 *buf = new (a, u8, s.len + 1); // is zeroed
  if (!buf) return (s8_){0};
  copy(buf, s.buf, s.len);
  return (s8_){ .v = {.buf = buf, .len = s.len + 1}}; 
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

/* Is s only whitespace? */
b32 s8blank(s8 s) {
  for (size i = 0; i < s.len; i++)
    if (!whitespace(s.buf[i]))
      return 0;
  return 1;
}

// Copies buf
s8_ s8clone(arena *a, s8 s) {
  u8 *buf = new (a, u8, s.len);
  if (!buf) return (s8_){0}; 
  s8 c = (s8){.buf = buf, .len = s.len};
  copy(c.buf, s.buf, s.len);
  return (s8_){.v = c}; // cast to union is apparently a gnu extension
}

/* Split s, returning s8spans referring to it (zero copy of buffer). */
// NB annoying choice between accepting s8 and making new s8 for no split
// vs accepting s8* and reusing for no split; former prob marginally better
s8s_ s8split(arena *store, arena scratch, s8 s, s8 on, size max_splits) {
  s8s_ nil = {0};
  u8 *end = endof(s);
  if (!s.buf) return nil; // exit early without allocating
  // Allocation-free hack to differentiate ok-but-empty from not-ok.
  if (s.len == 0) return (s8s_){ .v = { .buf = (s8 *)s.buf, .len = 0 } };
  u8 **matches = new (&scratch, u8 *, max_splits);
  if (!matches) return nil;
  size nmatches = 0;
  for (u8 *cur = s.buf; cur && cur < end && nmatches < max_splits;) {
    s8 rem = s8span(cur, end);
    cur = s8find(rem, on);
    if (cur) {
      matches[nmatches++] = cur;
      cur += on.len;
    }
  }
  s8 *buf = new (store, s8, nmatches + 1);
  if (!buf) return nil;
  if (nmatches == 0) buf[0] = s;
  else for (size nth = 0; nth <= nmatches; nth++) {
    if (nth == 0) {
      buf[nth] = s8span(s.buf, matches[nth]);
    } else if (nth == nmatches) {
      buf[nth] = s8span(matches[nth-1] + on.len, end);
    } else {
      buf[nth] = s8span(matches[nth-1] + on.len, matches[nth]);
    }
  }
  return (s8s_){.v = {.buf = buf, .len = nmatches + 1}};
}

// Trivially less efficient than impl calling s8findu8, but easier to maintain.
s8s_ s8splitu8(arena *store, arena scratch, s8 s, u8 on, size max_splits) {
  s8 ons = (s8){.buf = (u8[]){on}, .len = 1};
  return s8split(store, scratch, s, ons, max_splits);
}

// Concatenate array of strings
s8 s8concat(arena *a, s8 *ss, size len) {
  size tot = 0;
  for (size i = 0; i < len; i++) tot += ss[i].len;
  u8 *buf = new (a, u8, tot);
  if (!buf) return (s8){0};
  u8 *beg = buf;
  for (size i = 0; i < len; i++) {
    copy(beg, ss[i].buf, ss[i].len);
    beg += ss[i].len;
  }
  return (s8){.buf = buf, .len = tot};
}

// Present part of arena as s8.
s8_ s8arena(arena *buf, byte *from) {
  byte *f = from ? from : buf->beg;
  if (f >= buf->beg && f <= buf->cur)
    return (s8_){.v = {.buf = (u8 *)f, .len = buf->cur - f}};
  else return (s8_){0};
}

s8_ u8fill(arena *buf, u8 with, size count) {
  u8 *p = new (buf, u8, count);
  if (!p) return (s8_){0};
  for (size i = 0; i < count; i++) p[i] = with;
  return (s8_) { .v = s8span(p, p + count) };
}

// Intended for use with dedicated scratch!
s8_ s8build(arena *buf, s8 s) {
  u8 *cur = new (buf, u8, s.len);
  if (!cur) return (s8_){0};
  copy(cur, s.buf, s.len);
  return s8arena(buf, (byte *)cur);
}

// Intended for use with dedicated scratch!
s8_ s8sprintf(arena *buf, const char *format, ...) {
  if (!buf || !buf->cur) return (s8_){0};
  byte *start = buf->cur;
  va_list args;
  va_start(args, format);
  int n = vsnprintf(buf->cur, buf->end - buf->cur, format, args);
  va_end(args);
  if (n > 0) {
    buf->cur += n;
    return s8arena(buf, start);
  } else return (s8_){0};
}

// FIXME really want to be able to s8buildf(arena *buf, const char *format, ...)
// which can take s8s!
// This unfortunately requires copying because of the way s8s work (slice etc)
// So would need scratch arena.
// So should reconsider use of scratch as output/construction buffer.

// ────────────────────────────────────────────────────────────────────── Output

typedef struct {
  u8 *buf; // buffer itself, e.g. allocated with `new` macro
  size len; // current length of buffer contents
  size cap; // capacity of buffer, set at initialisation
  i32 fd; // 1 stdout, 2 stderr
  b32 err;
} bufout;

// FIXME deal with allocation failure!
#define bufout(a, n, f) &(bufout){.buf = new (a, u8, n), .cap = n, .fd = f}

void flush(bufout *b);

// Caller needs to flush
void s8write(bufout *b, s8 s) {
  if (!b->buf || !s.buf) return;
  u8 *buf = s.buf;
  u8 *end = endof(s);
  while (!b->err && (buf < end)) {
    i32 avail = b->cap - b->len; // TODO learn about size -> i32
    i32 count = (avail < end - buf) ? avail : (i32)(end - buf);
    // TODO learn benefit of copying rather than just stepping through s
    copy(b->buf + b->len, buf, count);
    buf += count;
    b->len += count;
    if (b->len == b->cap) flush(b);
  }
}

u32 oswrite(i32 fd, u8 *buf, i32 len);

// Should these indicate success?
void flush(bufout *b) {
  if (!b->err && b->len) {
      b->err = oswrite(b->fd, b->buf, b->len);
      b->len = 0;
    }
}

// Unbuffered, with newline
void s8log(i32 fd, s8 s, b32 newline) {
  oswrite(fd, (u8 *)s.buf, s.len);
  if (newline) oswrite(fd, (u8 *)"\n", 1);
}

// There's no shame in using prinf...
#define log_debug(s) s8log(1, s, 1);
#define log_err(s) s8log(2, s, 1);

// ──────────────────────────────────────────────────────────── Operating System

void osfail(i32 code);

void failwith(i32 code, s8 msg) {
  s8log(2, msg, 1);
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

#define inspect(ptr)                                 \
  s8log(1, s8("Contents of pointer " #ptr ":"), 1);  \
  debytes(1, ptr, sizeof(*(ptr)))

// TODO why are there so many signed ints below where negative is incorrect?

#ifdef _WIN32 // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ _WIN32

typedef struct { int dummy; } *handle;
#define W32(r) __declspec(dllimport) r __stdcall
W32(byte *) VirtualAlloc(byte *, usize, u32, u32);
W32(handle) GetStdHandle(u32);
W32(b32) ReadFile(handle, u8 *, u32, u32 *, void *);
W32(b32) WriteFile(handle, u8 *, u32, u32 *, void *);
W32(void) ExitProcess(u32);

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
  b32 ret;
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

i32 osread(i32 fb, u8 *buf, i32 cap) {
  handle stdin = GetStdHandle(-10 - fd);
  u32 len;
  ReadFile(stdin, buf, cap, &len, 0);
  return len;
}

u32 oswrite(i32 fd, u8 *buf, i32 len) {
  handle stdout = GetStdHandle(-10 - fd);
  u32 dummy;
  b32 stat = WriteFile(stdout, buf, len, &dummy, 0); // TODO GetLastError if fails
  if (stat) return 0;
  else return GetLastError();
}

// https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess
// void mainCRTStartup(void) { stuff; ExitProcess(code); }
        
#else // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ not _WIN32
 
#include <stdlib.h> // malloc
#include <unistd.h> // read write _exit
#include <errno.h>

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
  free(me);
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

// int main(void) { stuff; return r; }

#endif // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴

#endif // jdf_h
