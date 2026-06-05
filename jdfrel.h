/*
  Rework jdf.h for relative pointers (indices within arena) to ease
  arena resizing and maybe serialisation. Also drop win32. Changes API.
*/

#ifndef jdf_h
#define jdf_h

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

typedef uint8_t   u8;
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

#define alignof(x) (size)_Alignof(x) // casting from size_t
#define countof(arrayptr) (size)(sizeof(arrayptr) / sizeof(*(arrayptr))) // casting from size_t
#define new(a, t, n)                                            \
  (t *)alloc(a, sizeof(t), alignof(t), n, #t) // arena, type, number
#define DEBUG(...) fprintf(stderr, __VA_ARGS__);
/*
  Relative pointers, with respect to host arena (not anything else).
  Allows arena resizing and maybe serialisation. NB +1 keeps
  meaning of 0 as null.

  Operate on normal pointers, store relative pointers.

  Also see `alloc` comments.
  
  In contrast to non-rel implementation, need to pass "scratch" by
  reference to accommodate dynamic resize.
*/
typedef struct arena arena;
struct arena {
  byte *beg; // start of allocated memory
  byte *cur; // cursor: start of free space
  byte *end; // end of allocated memory
  // TODO 2026-06-04 19:50:05 check when calculating rel:
  arena *parent; // line of ancestors whose contents are fair game for a rel ptr 
};

struct rel {
  arena *arena; // allow pointing to ancestor arena contents
  size ptr; // byte offset within arena PLUS ONE
};

#define MAX_CAP PTRDIFF_MAX - 1

#define REL(t)                                                                 \
  typedef struct rel t##_rel_t;                                                \
  t##_rel_t t##_rel(arena *a, void *p) {                                       \
    if (!p)                                                                    \
      return (t##_rel_t){.arena = a, 0};                                       \
    byte *b = (byte *)p;                                                       \
    assert(a->beg <= b && b < a->cur && b < a->beg + MAX_CAP,                  \
           "invalid <t>_rel call");                                            \
    return (t##_rel_t){.arena = a, .ptr = b - a->beg + 1};                     \
  }                                                                            \
  t *t##_abs(t##_rel_t r) {                                                    \
    return r.ptr ? (t *)(r.arena->beg + r.ptr - 1) : 0;                        \
  }

#define rel(a, t, n) t##_rel(a, new (a, t, n))
#define ARRAY(tn, t) /* new type name, el type */                              \
  typedef struct {                                                             \
    union {                                                                    \
      t##_rel_t rel;                                                           \
      t *abs;                                                                  \
    };                                                                         \
    size len;                                                                  \
    b32 absolute;                                                              \
  } tn;                                                                        \
  tn make_##tn(arena *a, size len) {                                           \
    t##_rel_t r = rel(a, t, len);                                              \
    return r.ptr ? (tn){.rel = r, .len = len} : (tn){0};                       \
  }                                                                            \
  t##_rel_t tn##_array_rel(tn v, t *p) {                                       \
    assert(!v.absolute, "can't get rel for abs");                              \
    return t##_rel(v.rel.arena, p);                                            \
  }                                                                            \
  t *tn##_array_abs(tn v) { return v.absolute ? v.abs : t##_abs(v.rel); }      \
  t *tn##endof(tn v) { return tn##_array_abs(v) + v.len; }                     \
  /* Slice forward using pointers */                                           \
  tn tn##span(tn src, t *beg, t *end) {                                        \
    t *src_beg = tn##_array_abs(src);                                          \
    if (beg >= src_beg && end <= src_beg + src.len && end > beg)               \
      return src.absolute                                                      \
                 ? (tn){.abs = beg, .len = end - beg, .absolute = 1}           \
                 : (tn){.rel = tn##_array_rel(src, beg), .len = end - beg};    \
    return (tn){0};                                                            \
  }                                                                            \
  /*  Slice forward using clamped offsets, which may be positive or negative   \
      (i.e. from start or end, respectively) */                                \
  tn tn##slice(tn src, size from, size to) {                                   \
    tn s = src;                                                                \
    size f = (from < 0) ? src.len + from : from;                               \
    size t = (to > 0) ? to : src.len + to;                                     \
    if (f < 0)                                                                 \
      f = 0; /* clamp offsets */                                               \
    if (f > src.len)                                                           \
      f = src.len;                                                             \
    if (t < 0)                                                                 \
      t = 0;                                                                   \
    if (t > src.len)                                                           \
      t = src.len;                                                             \
    if (src.absolute)                                                          \
      s.abs += f;                                                              \
    else                                                                       \
      s.rel.ptr += f;                                                          \
    if (t > f)                                                                 \
      s.len = t - f;                                                           \
    else                                                                       \
      s.len = 0; /* refuse to slice backwards */                               \
    return s;                                                                  \
  }

/*
  To enable assertions in release builds,
  put UBSan in trap mode with -fsanitize-trap
  and then enable at least -fsanitize=unreachable.
*/
// TODO 2026-05-19 21:40:22 compiler flag to choose between:
#define assert(c, ...) while (!(c)) { printf("💥 "); printf(__VA_ARGS__); printf("\n"); __builtin_unreachable(); } // probably not optimised away?
// #define assert(c, ...) while (!(c)) __builtin_unreachable() // probably optimisable away

// ──────────────────────────────────────────────────────────────── Linked lists
typedef struct { size next; } node_t; // can be either direction; ignore subsequent fields; 0 indicates none, not self
node_t *offset(node_t *from, size by) { return (node_t *)((byte *)from + by); }
size ptrdiff(void *from, void *to) { return (byte *)to - (byte *)from; }
node_t *next(node_t *node) {
  assert(node, "no node for next");
  // NB 2026-05-02 12:54:00 need bounds checking elsewhere
  return node->next ? offset(node, node->next) : 0;
}
// No loop detection
size countfn(node_t *node) {
  assert(node, "no node for count");
  size c = 0;
  for(node_t *cur = node; cur; cur = next(cur)) c++;
  return c;
}
// Indirections to allow use from multiple linked list-derived data structures...
#define count(n) countfn((node_t *)n)
// No loop detection
node_t *nth(node_t *node, size n) {
  assert(node, "no node for nth");
  node_t *ret = node;
  for (size i = 0; i < n && ret; i++, ret = next(ret)); 
  return ret;
  }
// No loop detection!
node_t *last(node_t *node) {
  assert(node, "no node for last");
  node_t *prev = 0;
  for(; node; prev = node, node = next(node));
  return prev;
}
// Connect two nodes from same arena. Can cause loop! Returns any previous `from` tail.
// FIXME 2026-05-02 13:03:05 doesn't validate they're in same arena!
node_t *extend(node_t *from, node_t *to) {
  assert(from, "no node to extend");
  node_t *from_tail = next(from);
  from->next = ptrdiff(from, to);
  return from_tail;
}
// Insert exactly `count` nodes from `from` after `after`, returning
// any remaining `from` tail (including if count > available: insert
// none and return all of `from`).
node_t *insert(node_t *after, node_t *from, size count) {
  assert(after, "no node after which to insert");
  if (!from || count <= 0) return from;
  node_t *to = nth(from, count - 1);
  if (to) {
    node_t *after_tail = next(after);
    node_t *from_tail = next(to);
    after->next = ptrdiff(after, from);
    to->next = ptrdiff(to, after_tail);
    return from_tail;
  } else return from;
}
/*
  Define new linked list type tn, element type t. t can be a (typedef'd)
  pointer, making a reference list.

  <tn>_append appends node with value `m` to node `maybe`.
  If `maybe` doesn't exist, starts a new list.
  If `maybe` already has a ->next, follows it to the end.
  Does not prevent inclusion of stack-allocated values in heap-allocated list!
*/

#define NEXT(tn)                                                               \
  tn *tn##_next(tn *node) { return (tn *)next((node_t *)node); }

// NB 2026-05-02 00:09:37 only supports LIST within same arena
#define LIST(tn, t)                                                            \
  typedef struct tn tn;                                                        \
  REL(tn)                                                                      \
  struct tn {                                                                  \
    size next; /* relative to this struct! no +1, unlike REL; 0 indicates      \
                  nothing, not self */                                         \
    t val;                                                                     \
  };                                                                           \
  tn *tn##_last(tn *node) { return (tn *)last((node_t *)node); }               \
  tn *tn##_append(arena *a, tn *maybe, t m) {                                  \
    tn##_rel_t maybe_rel = tn##_rel(a, maybe); /* allow new to realloc */      \
    tn *new_one = new (a, tn, 1);                                              \
    if (!new_one)                                                              \
      return 0;                                                                \
    tn *cur = maybe = tn##_abs(maybe_rel);                                     \
    new_one->val = m;                                                          \
    if (maybe) {                                                               \
      cur = tn##_last(maybe); /* avoid passing early nodes if hot */           \
      cur->next = ptrdiff(cur, new_one);                                       \
      return maybe;                                                            \
    }                                                                          \
    return new_one;                                                            \
  }                                                                            \
  NEXT(tn)                                                                     \
  tn *tn##_nth(tn *node, size n) { return (tn *)nth((node_t *)node, n); }      \
  tn *tn##_extend(tn *from, tn *to) {                                          \
    return (tn *)extend((node_t *)from, (node_t *)to);                         \
  }                                                                            \
  tn *tn##_insert(tn *after, tn *from, size n) {                               \
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
#define MAP_LIST(tn, kt, vt, keq)                                              \
  typedef struct tn tn;                                                        \
  REL(tn)                                                                      \
  struct tn {                                                                  \
    size next;                                                                 \
    kt key;                                                                    \
    vt val;                                                                    \
  };                                                                           \
  NEXT(tn)                                                                     \
  /* Uniquely associate key to value. Caller must ensure kv validity.          \
     Assoc to null head to make new association list.                          \
     Allows null val. Returns null pointer if new fails. */                    \
  tn *tn##_assoc(arena *a, tn *head, kt key, vt val) {                         \
    tn *beg = 0;                                                               \
    if (!head) {                                                               \
      beg = new (a, tn, 1);                                                    \
      if (!beg)                                                                \
        return 0;                                                              \
      beg->key = key;                                                          \
      beg->val = val;                                                          \
      return beg;                                                              \
    }                                                                          \
    beg = head;                                                                \
    tn *cur = beg;                                                             \
    tn *prev = 0;                                                              \
    for (; cur; prev = cur, cur = tn##_next(cur)) {                            \
      if (keq(cur->key, key)) {                                                \
        cur->val = val;                                                        \
        return beg;                                                            \
      }                                                                        \
    }                                                                          \
    tn##_rel_t prev_rel = tn##_rel(a, prev);                                   \
    tn##_rel_t beg_rel = tn##_rel(a, beg);                                     \
    cur = new (a, tn, 1);                                                      \
    prev = tn##_abs(prev_rel);                                                 \
    beg = tn##_abs(beg_rel);                                                   \
    if (!cur)                                                                  \
      return 0;                                                                \
    prev->next = ptrdiff(prev, cur);                                           \
    cur->key = key;                                                            \
    cur->val = val;                                                            \
    return beg;                                                                \
  }                                                                            \
  tn *tn##_dissoc(tn *head, kt key) {                                          \
    tn *cur = head;                                                            \
    tn *prev = 0;                                                              \
    for (; cur; prev = cur, cur = tn##_next(cur)) {                            \
      if (keq(cur->key, key)) {                                                \
        if (prev)                                                              \
          prev->next = ptrdiff(prev, tn##_next(cur));                          \
        else                                                                   \
          return tn##_next(cur);                                               \
      }                                                                        \
    }                                                                          \
    return head;                                                               \
  }                                                                            \
  /* Return possibly-null pointer to kv pair with key match. */                \
  tn *tn##_get(tn *head, kt key) {                                             \
    assert(head, "no map to get from");                                        \
    tn *cur = head;                                                            \
    do {                                                                       \
      if (keq(cur->key, key))                                                  \
        return cur;                                                            \
    } while ((cur = tn##_next(cur)));                                          \
    return 0;                                                                  \
  }

// Barely worth it vs MAP_LIST with ignored vt. Make sure to use `disj`s returned head!
#define SET_LIST(tn, kt, keq)                                                  \
  typedef struct tn tn;                                                        \
  REL(tn)                                                                      \
  struct tn {                                                                  \
    size next;                                                                 \
    kt key;                                                                    \
  };                                                                           \
  NEXT(tn)                                                                     \
  tn *tn##_conj(arena *a, tn *head, kt key) {                                  \
    tn *beg = 0;                                                               \
    if (!head) {                                                               \
      beg = new (a, tn, 1);                                                    \
      if (!beg)                                                                \
        return 0;                                                              \
      beg->key = key;                                                          \
      return beg;                                                              \
    }                                                                          \
    beg = head;                                                                \
    tn *cur = beg;                                                             \
    tn *prev = 0;                                                              \
    for (; cur; prev = cur, cur = tn##_next(cur))                              \
      if (keq(cur->key, key))                                                  \
        return beg;                                                            \
    tn##_rel_t prev_rel = tn##_rel(a, prev);                                   \
    cur = new (a, tn, 1);                                                      \
    if (!cur)                                                                  \
      return 0;                                                                \
    prev = tn##_abs(prev_rel);                                                 \
    prev->next = ptrdiff(prev, cur);                                           \
    cur->key = key;                                                            \
    return beg;                                                                \
  }                                                                            \
  tn *tn##_disj(tn *head, kt key) {                                            \
    if (!head)                                                                 \
      return 0;                                                                \
    tn *cur = head;                                                            \
    tn *prev = 0;                                                              \
    for (; cur; prev = cur, cur = tn##_next(cur)) {                            \
      if (keq(cur->key, key)) {                                                \
        if (prev)                                                              \
          prev->next = ptrdiff(prev, tn##_next(cur));                          \
        else                                                                   \
          return tn##_next(cur);                                               \
      }                                                                        \
    }                                                                          \
    return head;                                                               \
  }                                                                            \
  tn *tn##_has(tn *head, kt key) {                                             \
    if (!head)                                                                 \
      return 0;                                                                \
    tn *cur = head;                                                            \
    do {                                                                       \
      if (keq(cur->key, key))                                                  \
        return cur;                                                            \
    } while ((cur = tn##_next(cur)));                                          \
    return 0;                                                                  \
  }

// ─────────────────────────────────────────────────────────────────────── Arena

size capacity(arena *a) { return a->end - a->beg; }
size used(arena *a) { return a->cur - a->beg; }
size available(arena *a) { return a->end - a->cur; }
b32 resize_arena(arena *a, size cap); // forward declaration

/*
  Allocate space within arena. Use via `new` macro.
  Not designed to be threadsafe! Each thread requires its own arena/s.

  Every allocation can cause the arena to resize, so take a relative
  pointer to anything which could change, before allocating, then
  convert back to absolute, after.
*/
byte *alloc(arena *a, size objsize, size align, size count, const char *t) {
  if (count <= 0 || align <= 0) return 0;
  printf("%p:%p [ %s ] of size %ti x %ti\n", (void *)a, (void *)a->beg, t, objsize, count);
  size padding = 0;
 recalc:
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
  padding = -(uptr)a->cur & (align - 1);
  size avail = available(a);
  size usd = used(a);
  /*
    Deliberately return null pointer if arena can't allocate requested amount!
    This does propagate annoyingly.
    Distinction between OOM proper and getting killed by (Linux) OOM killer?
  */
  if (count > (avail - padding) / objsize) {
    size needed = (count * objsize) + usd + padding;
    size c = capacity(a);
    size asking = c;
    // grug approve
    for (size thinking = asking; thinking < PTRDIFF_MAX; thinking *= 2) {
      if (thinking > needed) {
        asking = thinking;
        break;
      }
    }
    DEBUG("%p resizing arena from %ti to %ti\n", (void *)a, c, asking);
    if (resize_arena(a, asking)) {
      goto recalc;
    } else {
      // TODO 2026-05-23 18:47:09 more conservative size increment algo?
      fprintf(stderr, "Couldn't allocate %s: count %ti, count available %ti, capacity %ti\n",
              t, count, (avail - padding) / objsize, c);
      return 0;
    }
  }
  size total = count * objsize;
  byte *p = a->cur + padding;
  a->cur += padding + total;
  for (size i = 0; i < total; i++) p[i] = 0;
  return p;
}

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

REL(u8)
ARRAY(s8, u8) // s8: Basic UTF-8 string. Not null terminated!
#define s8(s) (s8){.abs = (u8 *)s, .len = countof(s) - 1, .absolute = 1}
static const s8 s8_OOM = s8("error: out of memory");
REL(s8)
LIST(s8l, s8)

b32 s8equal(s8 a, s8 b) {
  if (a.len != b.len) return 0;
  u8 *acur = s8_array_abs(a), *bcur = s8_array_abs(b);
  for (size i = 0; i < a.len ; i++) if (acur[i] != bcur[i]) return 0;
  return 1;
}

size s8cmp(s8 a, s8 b) {
  size len = (a.len < b.len) ? a.len : b.len;
  u8 *acur = s8_array_abs(a), *bcur = s8_array_abs(b);
  for (size i = 0; i < len; i++) {
    size d = acur[i] - bcur[i];
    if (d) return d;
  }
  return a.len - b.len;
}

// Why `size`? TODO visualise hashification of input
size s8hash(s8 s) {
  u64 h = 0x100;
  u8 *scur = s8_array_abs(s);
  for (size i = 0; i < s.len; i++) {
    h ^= scur[i];
    h *= 1111111111111111111u; // nineteen ones
  }
  return (h ^ h>>32) & (u32)-1;
}

// Find string
u8 *s8find(s8 haystack, s8 needle) {
  u8 *hcur = s8_array_abs(haystack), *ncur = s8_array_abs(needle);
  if (!hcur || !ncur) return 0;
  u8 *found = 0;
  u8 *he = hcur + haystack.len;
  u8 *ne = ncur + needle.len;
  // init first; cond before loop; iter after loop
  for (u8 *h = hcur; !found && (h < he); h++) {
    for (u8 *n = ncur;
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
  u8 *hcur = s8_array_abs(haystack);
  if (!hcur) return 0; // allow \0 needle
  u8 *end = hcur + haystack.len;
  for (u8 *h = hcur; h < end; h++)
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

// Wrap decayed C string
s8 s8wrap(const char *cstr, size maxlen) {
  if (!cstr) return (s8){0};
  u8 *beg = (u8 *)cstr;
  u8 *end = beg;
  while (*end != '\0' && (end-beg) < maxlen) end++;
  return (s8){.abs = beg, .len = end - beg, .absolute = 1};
}

// Return pointer to copy of s in a, one byte longer for terminal
// zero. Null if allocation fails. May be simpler to do manually with
// stack-allocated buffer for known-short strings.
char *s8unwrap(arena *a, s8 s) {
  u8 *buf = new (a, u8, s.len + 1); // is zeroed
  if (!buf) return 0;
  copy(buf, s8_array_abs(s), s.len);
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
  u8 *scur = s8_array_abs(src);
  u8 *beg = scur;
  u8 *end = scur + src.len;
  while (end > beg && whitespace(*(end - 1))) end--;
  while (beg < end && whitespace(*beg)) beg++;
  return s8span(src, beg, end);
}

/* Is s only whitespace? */
b32 s8blank(s8 s) {
  u8 *scur = s8_array_abs(s);
  for (size i = 0; i < s.len; i++)
    if (!whitespace(scur[i]))
      return 0;
  return 1;
}

// Copies buffer, optionally null-terminated for easier interop.
s8 s8clone(arena *a, s8 s, b32 null_terminate) {
  s8 c = make_s8(a, null_terminate ? s.len + 1 : s.len);
  if (!c.len) return c;
  copy(s8_array_abs(c), s8_array_abs(s), s.len);
  return c;
}

typedef struct {
  s8 head; // could be empty
  s8 tail;
  b32 ok;
} s8pair;

s8pair s8cut(s8 s, s8 on) {
  u8 *found = s8find(s, on);
  if (!found) return (s8pair){0};
  return (s8pair) {
    .head = s8span(s, s8_array_abs(s), found),
    .tail = s8span(s, found + on.len, s8endof(s)),
    .ok = 1
  };
}

s8pair s8cutu8(s8 s, u8 on) {
  u8 *found = s8findu8(s, on);
  if (!found) return (s8pair){0};
  return (s8pair) {
    .head = s8span(s, s8_array_abs(s), found),
    .tail = s8span(s, found + 1, s8endof(s)),
    .ok = 1
  };
}

// Concatenate array of strings
s8 s8concat(arena *a, s8 *ss, size len) {
  size tot = 0;
  for (size i = 0; i < len; i++) tot += ss[i].len;
  s8 ret = make_s8(a, tot);
  if (!ret.len) return ret;
  u8 *cur = s8_array_abs(ret);
  for (size i = 0; i < len; i++) {
    copy(cur, s8_array_abs(ss[i]), ss[i].len);
    cur += ss[i].len;
  }
  return ret;
}

size s8l_len(s8l *sl) {
  size len = 0;
  s8l *node = sl;
  do { len += node->val.len; } while ((node = s8l_next(node)));
  return len;
}

s8 s8l_concat(arena *store, s8l *sl) {
  s8 ret = make_s8(store, s8l_len(sl));
  if (!ret.len) return ret;
  u8 *cur = s8_array_abs(ret);
  s8l *node = sl;
  do {
    copy(cur, s8_array_abs(sl->val), sl->val.len);
    cur += sl->val.len;
  } while ((node = s8l_next(node)));
  return ret;
}

s8 u8fill(arena *a, u8 with, size count) {
  s8 ret = make_s8(a, count);
  if (!ret.len) return ret;
  u8 *cur = s8_array_abs(ret);
  for (size i = 0; i < count; i++) cur[i] = with;
  return ret;
}

// Does effectively allocate by moving buf.cur, so can fail.
// Also see s8printf.
s8 s8sprintf(arena *buf, const char *format, ...) {
  if (!buf || !buf->cur) return (s8){0};
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
    return (s8) {
      .rel = u8_rel(buf, start), .len = buf->cur - start
    };
  } else return (s8){0};
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
// NB element load must be atomic e.g. _Atomic struct job slots[LEN]
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
// TODO 2025-10-02 03:08:56 this is just me; need to test, analyse!
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
  *q += 0x1;
}
// ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Concurrent output buffer
typedef struct {
  s8 buf;
  queue q;
} qout;

qout make_qout(arena *a, i32 len) {
  qout nil = (qout){0};
  i32 cap = queue_capacity(len);
  if (!cap) return nil;
  u8 *buf = new (a, u8, len);
  if (!buf) return nil;
  return (qout) {.buf = (s8){.rel = u8_rel(a, buf), .len = len}, .q = 0 } ;
}
size read_qout(qout *qo, s8 buf) {
  i32 qi = 0;
  size bi = 0;
  u8 *abuf = s8_array_abs(buf), *qbuf = s8_array_abs(qo->buf);
  while ((qi = queue_pop(&qo->q, qo->buf.len))) { // where .len is queue capacity
    if (qi < 0) break; // empty
    abuf[bi++] = qbuf[qi];
    queue_pop_commit(&qo->q);
  }
  return bi;
}
size write_qout(qout *qo, u8 *buf, size maxlen) {
  i32 qi = 0;
  i32 bi = 0;
  u8 *qbuf = s8_array_abs(qo->buf);
  while (1) {
    qi = queue_push(&qo->q, qo->buf.len); // where .len is queue capacity
    if (qi < 0 || bi >= maxlen) break;
    // printf("pushing %c to queue position %i\n", buf[bi], qi);
    // printf("%c", buf[bi]);
    qbuf[qi] = buf[bi++]; // byte at a time
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

bufout make_bufout(arena *a, i32 cap, i32 fd) {
  u8 *buf = new (a, u8, cap);
  if (!buf) return (bufout){0};
  return (bufout){.buf = buf, .cap = cap, .fd = fd};
}

void flush(bufout *b);

typedef size (*Writer)(void *out, s8 s);

// Caller needs to flush
size s8write(void *out, s8 s) {
  bufout *b = (bufout *)out;
  if (!b->buf || !s.len) return 0;
  u8 *buf = s8_array_abs(s);
  u8 *end = s8endof(s);
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
i32 s8printf(arena *scratch, Writer writer, void *out, const char *format, ...) {
  if (!scratch->beg) return -1;
  assert(scratch->beg == scratch->cur, "scratch buffer cursor not at beginning");
  size avail = available(scratch); // TODO 2026-05-23 23:35:59 could resize if needed
  va_list args;
  va_start(args, format);
  // returns misleading n which disregards available size!
  // also disregards terminal \0, as usual
  i32 n = vsnprintf(scratch->beg, avail, format, args);
  va_end(args);
  if (n > 0) {
    scratch->cur += (n > avail ? avail : n); // at terminal \0
    return writer(out, (s8){.abs = (u8 *)scratch->beg,
                            .len = used(scratch),
                            .absolute = 1});
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

void s8writefd(i32 fd, s8 s) {oswrite(fd, s8_array_abs(s), s.len);}

// Unbuffered
void s8log(i32 fd, s8 s) {
  s8writefd(fd, s);
  oswrite(fd, (u8 *)"\n", 1);
}

// ──────────────────────────────────────────────────────────── Operating System

 // terminate without cleanup https://stackoverflow.com/a/5423108/780743

#define failwith(code, ...)                                                    \
  do {                                                                         \
    fprintf(stderr, __VA_ARGS__);                                              \
    _Exit(code);                                                               \
  } while (0)

// ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Arg parsing

ARRAY(plainargs, s8)
MAP_LIST(kvargs, s8, void *, s8equal)
enum argtype {UNK_ARG, INT_ARG, STR_ARG, BOOL_ARG};
MAP_LIST(argtypes, s8, enum argtype, s8equal)
  
struct args {
  kvargs *kv;
  plainargs rest;
};

s8 *str_arg(struct args a, char *k) {
  kvargs *kv = kvargs_get(a.kv, s8wrap(k, 64));
  return kv ? (s8 *)kv->val : 0;
}

b32 *bool_arg(struct args a, char *k) {
  kvargs *kv = kvargs_get(a.kv, s8wrap(k, 64));
  return kv ? (b32 *)kv->val : 0;
}

i32 *int_arg(struct args a, char *k) {
  kvargs *kv = kvargs_get(a.kv, s8wrap(k, 64));
  return kv ? (i32 *)kv->val : 0;
}

byte *reset_scratch(arena *a);

// Defs e.g. "--port=int --workers=int" must be in --long-arg=type form.
// Initials are promoted to short arg name (first wins).
// They will allow any combination of styles:
// "-p8080 -w3"
// "-p 8080 -w 3"
// "--port=8080" "--workers=3"
// "--port 8080" "--workers 3"
// and puts trailing args (or args after first "--") in .rest
struct args argparse(arena *store, arena *scratch, char *defs, int argc, char **argv) {
  argtypes *types = 0;
  s8pair def = {.tail = s8clone(scratch, s8wrap(defs, 1024), 0)};
  s8pair kv = {0};
  while (def.tail.len) {
    s8 remaining = def.tail;
    def = s8cut(def.tail, s8(" ")); // FIXME UB
    if (!def.head.len) def.head = remaining;
    kv = s8cut(def.head, s8("="));
    if (s8startswith(kv.head, s8("--"))) kv.head = s8slice(kv.head, 2, 0);
    else failwith(1, "Invalid arg name def in %s.", defs);
    enum argtype t = UNK_ARG;
    if (s8equal(s8("int"), kv.tail)) t = INT_ARG;
    else if (s8equal(s8("str"), kv.tail)) t = STR_ARG;
    else if (s8equal(s8("bool"), kv.tail)) t = BOOL_ARG;
    else failwith(1, "Invalid arg type def in %s.", defs);
    types = argtypes_assoc(scratch, types, kv.head, t);
  }
  struct args ret = {0};
  b32 await_val = 0;
  enum argtype t = UNK_ARG;
  int i = 1;
  for (; i < argc; i++) {
    s8 arg = s8wrap(argv[i], 256);
    if (!await_val) {
      kv = s8cut(arg, s8("="));
      kv = kv.head.len ? kv : (s8pair){.head = arg};
      if (s8equal(kv.head, s8("--"))) break; // with i set
      if (s8startswith(kv.head, s8("--"))) {
        // TODO 2026-05-26 12:07:50 support "--port 8080"
        kv.head = s8slice(kv.head, 2, 0);
      }
      else if (s8startswith(kv.head, s8("-"))) {
        if (kv.tail.len) failwith(1, "Invalid short arg format (omit '=').");
        kv.tail = s8slice(kv.head, 2, 0); // mutation in situ eugh
        kv.head = s8slice(kv.head, 1, 2);
        argtypes *cur = types;
        while (cur) 
          if (s8startswith(cur->key, kv.head)) {
            kv.head = cur->key;
            break;
          } else cur = argtypes_next(cur);
      } else { // allow absence of kwargs
        i--;
        break;
      }
      argtypes *kt = argtypes_get(types, kv.head);
      if (kt) t = kt->val; else t = STR_ARG; // default
      if (!kv.tail.len) {
        if (!(i + 1 == argc && t == BOOL_ARG)) {
          await_val = 1;
          continue;
        }
      }
    }
    i32 *i32p = 0;
    b32 *b32p = 0;
    s8 *s8p = 0;
    kv.tail = await_val ? arg : kv.tail; // .head = key, .tail = value
    switch (t) {
    case INT_ARG:
      i32p = new (scratch, i32, 1);
      if (!i32p) failwith(1, "OOM\n");
      kv.tail = s8wrap((char *)kv.tail.abs, 16); // should be null terminated
      char *end = 0;
      *i32p = strtol((char *)kv.tail.abs, &end, 10);
      if (!end) failwith(1, "Invalid int argument.");
      ret.kv = kvargs_assoc(store, ret.kv, kv.head, i32p);
      break;
    case BOOL_ARG:
      b32p = new (store, b32, 1); // initialised to 0 i.e. false
      if (!b32p) failwith(1, "OOM\n");
      *b32p = 1; // if none->true
      if (s8equal(kv.tail, s8("false"))) *b32p = 0;
      else if (s8equal(kv.tail, s8("true")) || !kv.tail.len); // already
      else if (s8startswith(kv.tail, s8("-"))) i-- ; // no value, would be next arg relook at this arg next loop
      else
        failwith(1, "Invalid bool argument.");
      ret.kv = kvargs_assoc(store, ret.kv, kv.head, b32p);
      break;
    case STR_ARG:
      s8p = new (scratch, s8, 1);
      if (!s8p) failwith(1, "OOM\n");
      if (s8startswith(kv.tail, s8("-"))) failwith(1, "Invalid str argument.");
      *s8p = kv.tail;
      ret.kv = kvargs_assoc(store, ret.kv, kv.head, s8p);
      break;
    default:
      if (s8equal(kv.tail, s8("--"))) {
        continue;
      }
    }
    kv = (s8pair){0};
    t = UNK_ARG;
    await_val = 0;
  }
  if (++i < argc) {
    plainargs rest = make_plainargs(store, argc - i);
    s8 *cur = plainargs_array_abs(rest);
    if (!rest.len) failwith(1, "OOM\n");
    for (int j = 0; j < argc - i; j++) 
      cur[j] = s8wrap(argv[i + j], 256);
    ret.rest = rest;
  }
  reset_scratch(scratch);
  return ret;
}

// ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Debug

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

#define inspect(ptr)                                                           \
  s8writefd(2, s8("Contents of pointer " #ptr ":"));                           \
  debytes(2, ptr, sizeof(*(ptr)))

#define log_debug(s)                                                           \
  s8writefd(2, s8("Value of " #s ": "));                                       \
  s8log(2, s)

// ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ Essentials

// TODO why are there so many signed ints below where negative is incorrect?
 
#include <stdlib.h> // malloc
#include <unistd.h> // read write _exit
#include <errno.h>

// malloc failure will return zero-capacity arena so its `alloc`s will just fail.
arena alloc_arena(usize cap, arena *parent) {
  assert(cap <= MAX_CAP, "arena too big to address");
  byte *beg = malloc(cap);
  DEBUG(":%p allocated %tuB for arena with parent %p\n", (void *)beg, cap, (void *)parent);
  if (beg) return (arena){.beg = beg, .cur = beg, .end = beg + cap, .parent = parent};
  else return (arena){0};
}

// Not threadsafe
b32 resize_arena(arena *a, size cap) {
  assert(cap > used(a), "can't shrink while full");
  usize u = used(a);
  byte *new_memory = realloc(a->beg, cap);
  //printf("a: %p, beg: %p, new: %p\n", a, a->beg, new_memory);
  DEBUG("%p:%p->%p reallocated %tuB with parent %p\n", (void *)a, (void *)a->beg, (void *)new_memory, cap, (void *)a->parent);
  if (new_memory) {
    a->beg = new_memory;
    a->cur = new_memory + u;
    a->end = new_memory + cap;
    return 1;
  } else {
    // "If there is not enough memory, the old memory block is not freed and null pointer is returned."
    fprintf(stderr, "Unable to grow arena\n");
    return 0;
  }
}

b32 free_arena(arena *a) {
  //printf("freeing arena %p\n", (void *a);
  byte *me = a->beg;
  a->beg = 0;
  a->cur = 0;
  a->end = 0;
  free(me); // safe even if null
  // NB 2026-05-02 13:59:49 not resetting id 
  // TODO 2026-05-01 17:59:16 mechanism for removing from arenas global (and notifying errors)?
  return 1;
}

byte *reset_scratch(arena *a) { // NB 2026-05-26 13:53:00 breaks arena concept a bit, only when exiting scope
  return (a->cur = a->beg);
}

// TODO 2026-05-24 01:50:58 ser/de arenas with a little metadata

i32 osread(i32 fd, u8 *buf, i32 cap) {
  return (i32)read(fd, buf, cap);
}

u32 oswrite(i32 fd, u8 *buf, i32 len) {
  for (i32 off = 0; off < len; ) {
    i32 r = (i32)write(fd, buf + off, len - off);
    if (r < 1) return errno; // TODO 2026-04-20 20:17:31 0 and EAGAIN? block here?
    off += r;
  }
  return 0;
}

// i32 main(void) { stuff; return r; }

#endif // jdf_h
