/*
  Trying to reject incidental complexity, including at build time.
  
  After Wellons https://nullprogram.com/blog/2023/10/08/
  all a bit controversial https://old.reddit.com/r/C_Programming/comments/173e0vn/nullprogram_my_personal_c_coding_style_as_of_late/
  esp wrt risk of name collision on public facing things.
  Consider fancy leading unicode character pseudonamespace?

  - omit const (controversial!)
  - literal 0 for null pointers
  - restrict when necessary
  - typedef all structures
  - static all functions except for entry points (adjusted so this header works)
  (alternative to #include "style.c" into every translation unit...)
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

#define sizeof(x) (size)sizeof(x)
#define alignof(x) (size)_Alignof(x)
#define countof(a) (sizeof(a) / sizeof(*(a)))
#define lengthof(s) (countof(s) - 1)
#define new(a, t, n) (t *)alloc(a, sizeof(t), alignof(t), n)

typedef struct {
  byte *beg;
  byte *end;
} arena;

void oom(void);
/* Allocate space within arena. Use via `new` macro. */
byte *alloc(arena *a, size objsize, size align, size count);
void copy(u8 *restrict dst, u8 *restrict src, size len);

/* To enable assertions in release builds, put UBSan in trap mode with
   -fsanitize-trap and then enable at least -fsanitize=unreachable. */
#define assert(c) while (!(c)) __builtin_unreachable()

/* Wrap C string literal into s8 string.*/
#define s8(s) (s8){(u8 *)s, lengthof(s)}
/* Multiline without quotes. Collapses whitespace. */
#define text(...) s8(#__VA_ARGS__) // https://stackoverflow.com/a/17996915/780743

/* Basic UTF-8 string. Not null terminated! */
typedef struct {
  u8 *buf;
  size len;
} s8;

typedef struct {
  u8 *buf;
  i32 len;
  i32 cap;
  b32 err;
} bufout;

s8 s8span(u8 *beg, u8 *end);
b32 s8equal(s8 a, s8 b);
size s8cmp(s8 a, s8 b);
size s8hash(s8 s);
s8 s8clone(arena *a, s8 s);
void flush(bufout *b);
void s8write(bufout *b, s8 s);

void osfail(void);
i32 osread(i32 fd, u8 *buf, i32 cap);
b32 oswrite(i32 fd, u8 *buf, i32 len);

#endif // jdf_h
