// Resist temptation to write whole testing framework...

#ifndef test_h
#define test_h

#include <stdio.h>
int passed_tests;
int total_tests;

void HEAD(char *title) {
  printf("\n👉 %s\n", title);
}

// Can't cope with compound literals... containing commas?
#define PREP(expr) expr; printf("%s;\n", #expr)

#define TEST(expr)                              \
  if (expr) {                                   \
    passed_tests++;                             \
    total_tests++;                              \
    printf("🟢");                               \
  } else {                                      \
    total_tests++;                              \
    printf("🔴");                               \
  }                                             \
  printf(" %s\n", #expr)

int REPORT(void) {
  printf("%i/%i passed\n", passed_tests, total_tests);
  return passed_tests != total_tests; // if equal, would return 0 i.e. success (conventionally)
}

#endif // test_h
