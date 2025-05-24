// This is auto-generated, do not edit!
#include "jdf.h"
#ifndef DATES_JSON
#define DATES_JSON
enum month {
  INVALID_MONTH,
  JANUARY = 1,
  FEBRUARY,
  MARCH,
  APRIL,
  MAY,
  JUNE,
  JULY,
  AUGUST,
  SEPTEMBER,
  OCTOBER,
  NOVEMBER,
  DECEMBER,
};
const char *spell_month[] = {
  [JANUARY] = "January",
  [FEBRUARY] = "February",
  [MARCH] = "March",
  [APRIL] = "April",
  [MAY] = "May",
  [JUNE] = "June",
  [JULY] = "July",
  [AUGUST] = "August",
  [SEPTEMBER] = "September",
  [OCTOBER] = "October",
  [NOVEMBER] = "November",
  [DECEMBER] = "December",
};
const char *describe_month[] = {
  [JANUARY] = "January",
  [FEBRUARY] = "February",
  [MARCH] = "March",
  [APRIL] = "April",
  [MAY] = "May",
  [JUNE] = "June",
  [JULY] = "July",
  [AUGUST] = "August",
  [SEPTEMBER] = "September",
  [OCTOBER] = "October",
  [NOVEMBER] = "November",
  [DECEMBER] = "December",
};
enum month parse_month(s8 s) {
  for (size i = 0; i < 12; i++)
    if(s8equal(s, s8wrap(spell_month[i], 1024)))
      return (enum month)i;
  return (enum month)0;
}
enum weekday {
  INVALID_WEEKDAY,
  SUNDAY = 0,
  MONDAY,
  TUESDAY,
  WEDNESDAY,
  THURSDAY,
  FRIDAY,
  SATURDAY,
};
const char *spell_weekday[] = {
  [SUNDAY] = "Sunday",
  [MONDAY] = "Monday",
  [TUESDAY] = "Tuesday",
  [WEDNESDAY] = "Wednesday",
  [THURSDAY] = "Thursday",
  [FRIDAY] = "Friday",
  [SATURDAY] = "Saturday",
};
const char *describe_weekday[] = {
  [SUNDAY] = "Sunday",
  [MONDAY] = "Monday",
  [TUESDAY] = "Tuesday",
  [WEDNESDAY] = "Wednesday",
  [THURSDAY] = "Thursday",
  [FRIDAY] = "Friday",
  [SATURDAY] = "Saturday",
};
enum weekday parse_weekday(s8 s) {
  for (size i = 0; i < 7; i++)
    if(s8equal(s, s8wrap(spell_weekday[i], 1024)))
      return (enum weekday)i;
  return (enum weekday)0;
}
#endif // DATES_JSON
