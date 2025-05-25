#include "jdfdate.h"
#include "test.h"

int main(void) {
  HEAD("date functions");
  TEST(date_epoch((date){1970,1,1})==0);
  TEST(date_epoch(epoch_date(0)) == 0);
  TEST(valid_date((date){2020, 1, 2}));
  TEST(!valid_date((date){2020, 13, 2}));
  TEST(date_equal((date){2020, 1, 2}, date_offset((date){2020, 2, 4}, -33)));
  HEAD("duration calculation");
  date d1 = (date){2018, 1, 1};
  date d2 = (date){2018, 1, 8};
  TEST(in_weeks(d1, d2) == 1);
  TEST(in_weeks(d1, d1) == 0);
  TEST(in_weeks(d2, d1) == -1);
  return REPORT();
}
