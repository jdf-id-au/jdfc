#include "jdfdate.h"
#include "test.h"

int main(void) {
  HEAD("date functions");
  TEST(date_epoch((date){1970,1,1})==0);
  TEST(date_epoch((date){1970, 1, 2}) == 1);
  TEST(date_epoch(epoch_date(0)) == 0);
  arena store = alloc_arena(MiB(1));
  log_debug(s8date(&store, epoch_date(58)).v);
  log_debug(s8date(&store, epoch_date(59)).v);
  TEST(valid_date((date){2020,1,2}));
  TEST(valid_date(date_offset((date){2020, 2, 4}, -33)));
  TEST(date_equal((date){2020,1,2}, (date){2020,1,2}));
  TEST(date_equal((date){2020, 1, 2}, date_offset((date){2020, 1, 1}, 1)));
  TEST(date_equal((date){2020, 1, 2}, date_offset((date){2020, 2, 4}, -33)));
  printf("%d %d wtf\n",
         date_epoch((date){2020, 1, 2}),
         date_epoch(date_offset((date){2020, 2, 4}, -33)));
  return REPORT();
}
