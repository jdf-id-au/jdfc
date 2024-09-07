/*
  Trying to reject incidental complexity, including at build time.

  After Wellons https://nullprogram.com/blog/2023/10/08/
  and https://nullprogram.com/blog/2023/09/27/ .
  All a bit controversial https://old.reddit.com/r/C_Programming/comments/173e0vn/nullprogram_my_personal_c_coding_style_as_of_late/
  esp wrt risk of name collision on public facing things.
  
  - omit const (controversial!)
  - literal 0 for null pointers
  - restrict when necessary
  - typedef all structures
  - static all functions except for entry points (not applied here)
  (alternative to #include "jdf.c" into every translation unit...)
  - structure returns instead of out parameters; initialise with {0} as per C99
*/

#ifndef jdf_h
#define jdf_h

#include <stddef.h>
#include <stdint.h>

typedef uint8_t   u8;
typedef int32_t   b32; // 0 false, 1 true
typedef int32_t   i32;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef float     f32;
typedef double    f64;
typedef uintptr_t uptr;
typedef char      byte;
typedef ptrdiff_t size;
typedef size_t    usize;

#pragma GCC diagnostic push // also understood by clang
#pragma GCC diagnostic ignored "-Wkeyword-macro"
#define sizeof(x) (size)sizeof(x) // casting from ?impl-defined unsigned int type
#pragma GCC diagnostic pop

#define alignof(x) (size)_Alignof(x) // casting from size_t
#define countof(a) (sizeof(a) / sizeof(*(a))) // relies on a not having decayed
#define lengthof(s) (countof(s) - 1)
#define new(a, t, n) (t *)alloc(a, sizeof(t), alignof(t), n) // arena, type, number
#define sized(tn, t) typedef struct { t *buf; size len; } tn // new type name, el type
#define endof(v) v.buf + v.len // last of sized value

enum errors {EPARSING = 1000, EMEMORY, EREF};

// ──────────────────────────────────────────────────────────────── Linked lists

#define count_decl(tn) size count##tn(tn *v)
#define count_impl(tn)                          \
  count_decl(tn) {                              \
    size c = 0;                                 \
    tn *cur = v;                                \
    while (cur->next) {                         \
      c++;                                      \
      cur = cur->next;                          \
    }                                           \
    return c;                                   \
  }

#define value_append_decl(tn, t) tn *tn##append(arena *a, tn *maybe, t m)
#define value_append_impl(tn, t)                \
  value_append_decl(tn, t) {                    \
    tn *cur = new (a, tn, 1);                   \
    cur->val = m;                               \
    if (maybe) maybe->next = cur;               \
    return cur;                                 \
  }
#define value_list(tn, t)                       \
  typedef struct tn tn;                         \
  struct tn {                                   \
    t val;                                      \
    tn *next;                                   \
  };                                            \
  count_decl(tn);                               \
  value_append_decl(tn, t)

#define ref_append_decl(tn, t) tn *tn##append(arena *a, tn *maybe, t *m)
#define ref_append_impl(tn, t)                  \
  ref_append_decl(tn, t) {                      \
    tn *cur = new (a, tn, 1);                   \
    cur->val = m;                               \
    if (maybe) maybe->next = cur;               \
    return cur;                                 \
  }
#define ref_list(tn, t)                         \
  typedef struct tn tn;                         \
  struct tn {                                   \
    t *val;                                     \
    tn *next;                                   \
  };                                            \
  count_decl(tn);                               \
  ref_append_decl(tn, t)

// ─────────────────────────────────────────────────────────────────────── Arena

/*
  Pass "store" arena by reference, and "scratch" by value.
  This effectively resets the scratch *beg pointer on fn return.
*/
typedef struct {
  byte *beg;
  byte *end;
} arena;

void oom(void);
size KiB(u32 n);
size MiB(u32 n);
// Allocate space within arena. Use via `new` macro.
byte *alloc(arena *a, size objsize, size align, size count);

void copy(u8 *restrict dst, u8 *restrict src, size len);

/*
  To enable assertions in release builds,
  put UBSan in trap mode with -fsanitize-trap
  and then enable at least -fsanitize=unreachable.

  FIXME would be better with error message...
*/
#define assert(c) while (!(c)) __builtin_unreachable()

typedef struct {
  u8 *buf; // buffer itself, e.g. allocated with `new` macro
  size len; // current length of buffer contents
  size cap; // capacity of buffer, set at initialisation
  i32 fd; // 1 stdout, 2 stderr
  b32 err;
} bufout;
#define bufout(a, n, f) &(bufout){.buf = new(a, u8, n), .cap = n, .fd = f}

// ───────────────────────────────────────────────────────────────────── Strings

sized(s8, u8); // Basic UTF-8 string. Not null terminated!
#define s8(s) (s8){(u8 *)s, lengthof(s)} // Wrap C string literal into s8 string.

/*
  Multiline without quotes. Collapses whitespace.
  IDE may be annoying about it, try fundamental-mode.
*/ 
#define text(...) s8(#__VA_ARGS__) // https://stackoverflow.com/a/17996915/780743

s8 s8span(u8 *beg, u8 *end);
// offsets may be positive or negative (i.e. from start or end, respectively)
s8 s8slice(s8 s, size from, size to);
b32 s8equal(s8 a, s8 b);
size s8cmp(s8 a, s8 b);
size s8hash(s8 s);
u8 *s8find(s8 haystack, s8 needle);
u8 *s8findc(s8 haystack, u8 needle);
s8 s8wrap(u8 *cstr, size maxlen);
s8 s8trim(s8 s);
s8 s8fill(arena *a, u8 with, size count);
s8 s8clone(arena *a, s8 s);
s8 s8concat(arena *a, s8 *ss, size len);
void s8write(bufout *b, s8 s); // caller needs to flush
void s8writeln(bufout *b, s8 s); // caller needs to flush

// ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ List of strings

value_list(s8s, s8);
s8 s8sconcat(arena *a, s8s *s);

// ──────────────────────────────────────────────────────────── Operating System

void flush(bufout *b);
void error(i32 code, s8 msg);

void debug(s8 msg); // unbuffered
void debytes_impl(void *val, size len);
// silly portmaneau
#define debytes(ptr) debug(s8(#ptr)); debytes_impl(ptr, sizeof(*(ptr)))

#ifndef _WIN32 // ╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴╴ not _WIN32

arena malloc_arena(size cap);
void osfail(i32 code);
i32 osread(i32 fd, u8 *buf, i32 cap);
b32 oswrite(i32 fd, u8 *buf, i32 len);

#endif // not _WIN32

#endif // jdf_h
