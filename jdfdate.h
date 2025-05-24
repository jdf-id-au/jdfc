#include "jdf.h"
#include "dates.h"
#include <limits.h>

#ifndef date_h
#define date_h

// after https://howardhinnant.github.io/date_algorithms.html

typedef i32 epoch; // Number of days since 1970-01-01, can be negative.
typedef i32 year;
typedef u8 month; // January is 1
typedef u8 day; // 1st is 1
typedef u8 weekday; // Sunday is 0 

typedef struct {
  year y;
  month m;
  day d;
} date;

MAYBE(date)

const char *date_format = "%4d-%02hhd-%02hhd"; // yyyy-mm-dd

b32 is_leap(year y) {
  return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
}

day last_day_of_month_common_year(month m) {
  day a[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return a[m-1];
}

day last_day_of_month_leap_year(month m) {
  day a[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return a[m-1];
}

day last_day_of_month(date ym) {
  return ym.m != 2 || !is_leap(ym.y) ? last_day_of_month_common_year(ym.m) : 29;
}

epoch date_epoch(date date) {
  date.y -= date.m <= 2;
  i32 era = (date.y >= 0 ? date.y : date.y - 399) / 400;
  u32 yoe = date.y - era * 400;
  u32 doy = (153 * (date.m > 2 ? date.m - 3 : date.m + 9) + 2) / 5 + date.d - 1;
  u32 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

date epoch_date(epoch z) {
  date ret = {0};
  z += 719468;
  i32 era = (z >= 0 ? z : z - 146096)/146097;
  u32 doe = z - era*146097;
  u32 yoe = (doe - doe/1460 + doe/36524 - doe/146096)/365;
  ret.y = yoe + era*400;
  u32 doy = doe - (365*yoe + yoe/4 - yoe/100);
  u32 mp = (5*doy + 2)/153;
  ret.d = doy - (153*mp + 2)/5 + 1;
  ret.m = mp < 10 ? mp + 3 : mp - 9;
  return ret;
}

weekday epoch_weekday(epoch z) {
  return z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6;
}

b32 valid_date(date date) {
  return date.y > 1582 && date.y <= 9999 && //epoch_date(INT_MAX - 719468).y &&
    date.m >= JANUARY && date.m <= DECEMBER &&
    date.d >=1 && date.d <= last_day_of_month(date);
}

b32 date_equal(date d1, date d2) {
  return date_epoch(d1) == date_epoch(d2);
}

date date_offset(date d, i32 days) {
  return epoch_date(date_epoch(d) + days);
}

s8_ s8date(arena *store, date d) {
  return s8sprintf(store, date_format, d.y, d.m, d.d);
}

date_ s8parsedate(s8 s) {
  if (s.len != 10) return (date_){0};
  u8 buf[11] = {0}; // make zero-terminated ugh local s8unwrap
  copy(buf, s.buf, 10);
  date_ ret = {0};
  if (sscanf((const char *)buf, date_format, &ret.v.y, &ret.v.m, &ret.v.d) == 3)
    return ret;
  return (date_){0};
}

#endif // date_h
