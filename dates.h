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
static const s8 spell_month[] = {
  [JANUARY] = s8("January"),
  [FEBRUARY] = s8("February"),
  [MARCH] = s8("March"),
  [APRIL] = s8("April"),
  [MAY] = s8("May"),
  [JUNE] = s8("June"),
  [JULY] = s8("July"),
  [AUGUST] = s8("August"),
  [SEPTEMBER] = s8("September"),
  [OCTOBER] = s8("October"),
  [NOVEMBER] = s8("November"),
  [DECEMBER] = s8("December"),
};
static const s8 describe_month[] = {
  [JANUARY] = s8("January"),
  [FEBRUARY] = s8("February"),
  [MARCH] = s8("March"),
  [APRIL] = s8("April"),
  [MAY] = s8("May"),
  [JUNE] = s8("June"),
  [JULY] = s8("July"),
  [AUGUST] = s8("August"),
  [SEPTEMBER] = s8("September"),
  [OCTOBER] = s8("October"),
  [NOVEMBER] = s8("November"),
  [DECEMBER] = s8("December"),
};
enum month parse_month(s8 s) {
  for (size i = 0; i < 12; i++)
    if(s8equal(s, spell_month[i]))
      return (enum month)i;
  return (enum month)0;
}
enum weekday {
  SUNDAY = 0,
  MONDAY,
  TUESDAY,
  WEDNESDAY,
  THURSDAY,
  FRIDAY,
  SATURDAY,
};
static const s8 spell_weekday[] = {
  [SUNDAY] = s8("Sunday"),
  [MONDAY] = s8("Monday"),
  [TUESDAY] = s8("Tuesday"),
  [WEDNESDAY] = s8("Wednesday"),
  [THURSDAY] = s8("Thursday"),
  [FRIDAY] = s8("Friday"),
  [SATURDAY] = s8("Saturday"),
};
static const s8 describe_weekday[] = {
  [SUNDAY] = s8("Sunday"),
  [MONDAY] = s8("Monday"),
  [TUESDAY] = s8("Tuesday"),
  [WEDNESDAY] = s8("Wednesday"),
  [THURSDAY] = s8("Thursday"),
  [FRIDAY] = s8("Friday"),
  [SATURDAY] = s8("Saturday"),
};
enum weekday parse_weekday(s8 s) {
  for (size i = 0; i < 7; i++)
    if(s8equal(s, spell_weekday[i]))
      return (enum weekday)i;
  return (enum weekday)0;
}
#endif // DATES_JSON
