/* SPDX-License-Identifier: MIT */
/* Date & Time's arithmetic and words. See clock.h. */
#include "clock.h"

#include <stdio.h>
#include <string.h>

/* West to east, so left and right move the way a map does. */
const clock_zone CLOCK_ZONES[] = {
	{ "Pacific/Honolulu",    "Honolulu", "HST10" },
	{ "America/Anchorage",   "Anchorage", "AKST9AKDT,M3.2.0,M11.1.0" },
	{ "America/Los_Angeles", "Los Angeles", "PST8PDT,M3.2.0,M11.1.0" },
	{ "America/Phoenix",     "Phoenix", "MST7" },
	{ "America/Denver",      "Denver", "MST7MDT,M3.2.0,M11.1.0" },
	{ "America/Chicago",     "Chicago", "CST6CDT,M3.2.0,M11.1.0" },
	{ "America/Mexico_City", "Mexico City", "CST6" },
	{ "America/New_York",    "New York", "EST5EDT,M3.2.0,M11.1.0" },
	{ "America/Halifax",     "Halifax", "AST4ADT,M3.2.0,M11.1.0" },
	{ "America/St_Johns",    "St. John's", "NST3:30NDT,M3.2.0,M11.1.0" },
	{ "America/Sao_Paulo",   "Sao Paulo", "<-03>3" },
	{ "UTC",                 "UTC", "UTC0" },
	{ "Europe/London",       "London", "GMT0BST,M3.5.0/1,M10.5.0" },
	{ "Europe/Paris",        "Paris", "CET-1CEST,M3.5.0,M10.5.0/3" },
	{ "Europe/Berlin",       "Berlin", "CET-1CEST,M3.5.0,M10.5.0/3" },
	{ "Africa/Lagos",        "Lagos", "WAT-1" },
	{ "Europe/Athens",       "Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
	{ "Africa/Johannesburg", "Johannesburg", "SAST-2" },
	{ "Europe/Moscow",       "Moscow", "MSK-3" },
	{ "Asia/Dubai",          "Dubai", "<+04>-4" },
	{ "Asia/Karachi",        "Karachi", "PKT-5" },
	{ "Asia/Kolkata",        "India", "IST-5:30" },
	{ "Asia/Dhaka",          "Dhaka", "<+06>-6" },
	{ "Asia/Bangkok",        "Bangkok", "<+07>-7" },
	{ "Asia/Shanghai",       "Shanghai", "CST-8" },
	{ "Asia/Singapore",      "Singapore", "<+08>-8" },
	{ "Asia/Tokyo",          "Tokyo", "JST-9" },
	{ "Australia/Adelaide",  "Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
	{ "Australia/Sydney",    "Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
	{ "Pacific/Auckland",    "Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3" },
};
const int CLOCK_ZONE_COUNT = (int)(sizeof CLOCK_ZONES / sizeof CLOCK_ZONES[0]);

static const char *const MONTHS[12] = {
	"January", "February", "March", "April", "May", "June", "July",
	"August", "September", "October", "November", "December",
};

static int days_in(int year, int mon)
{
	static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	if (mon == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
		return 29;
	return days[mon];
}

/* v brought back into lo..hi, going round */
static int wrap(int v, int lo, int hi)
{
	int span = hi - lo + 1;

	return ((v - lo) % span + span) % span + lo;
}

time_t clock_step(time_t t, clock_field f, int d)
{
	struct tm tm;
	int year, last;

	localtime_r(&t, &tm);
	tm.tm_sec = 0;
	switch (f) {
	case CLK_YEAR:
		/* Stops rather than going round: 2099 to 2000 is never what a
		 * press of right meant */
		year = tm.tm_year + 1900 + d;
		if (year < 2000) year = 2000;
		if (year > 2099) year = 2099;
		tm.tm_year = year - 1900;
		break;
	case CLK_MONTH:  tm.tm_mon  = wrap(tm.tm_mon + d, 0, 11); break;
	case CLK_DAY:
		tm.tm_mday = wrap(tm.tm_mday + d, 1, days_in(tm.tm_year + 1900, tm.tm_mon));
		break;
	case CLK_HOUR:   tm.tm_hour = wrap(tm.tm_hour + d, 0, 23); break;
	case CLK_MINUTE: tm.tm_min  = wrap(tm.tm_min + d, 0, 59); break;
	default: break;
	}
	/* January 31 moved to February is February's last day, not March 3 */
	last = days_in(tm.tm_year + 1900, tm.tm_mon);
	if (tm.tm_mday > last) tm.tm_mday = last;
	tm.tm_isdst = -1;   /* the zone decides, for the date it now is */
	return mktime(&tm);
}

void clock_field_label(time_t t, clock_field f, char *out, size_t n)
{
	struct tm tm;
	int h;

	localtime_r(&t, &tm);
	h = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;
	switch (f) {
	case CLK_YEAR:   snprintf(out, n, "%d", tm.tm_year + 1900); break;
	case CLK_MONTH:  snprintf(out, n, "%s", MONTHS[tm.tm_mon]); break;
	case CLK_DAY:    snprintf(out, n, "%d", tm.tm_mday); break;
	case CLK_HOUR:   snprintf(out, n, "%d %s", h, tm.tm_hour < 12 ? "AM" : "PM"); break;
	case CLK_MINUTE: snprintf(out, n, "%02d", tm.tm_min); break;
	default:         snprintf(out, n, "%s", ""); break;
	}
}

void clock_label(time_t t, char *out, size_t n)
{
	struct tm tm;
	int h;

	localtime_r(&t, &tm);
	h = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;
	snprintf(out, n, "%.3s %d, %d:%02d %s", MONTHS[tm.tm_mon], tm.tm_mday,
	         h, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
}

int clock_zone_find(const char *id)
{
	int i;

	if (!id) return -1;
	for (i = 0; i < CLOCK_ZONE_COUNT; i++)
		if (!strcmp(CLOCK_ZONES[i].id, id)) return i;
	return -1;
}

int clock_zone_step(const char *id, int d)
{
	int i = clock_zone_find(id);

	if (i < 0) return clock_zone_find("America/New_York");
	i += d;
	if (i < 0) i = 0;
	if (i >= CLOCK_ZONE_COUNT) i = CLOCK_ZONE_COUNT - 1;
	return i;
}
