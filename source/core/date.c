#include "date.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Howard Hinnant's days_from_civil / civil_from_days, no time zones involved. */
static long days_from_civil(long y, unsigned m, unsigned d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static void civil_from_days(long z, long *y, unsigned *m, unsigned *d)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yy + (*m <= 2);
}

int date_add_days(const char *date, int delta, char out[CT_DATE_LEN])
{
    int y, m, d;
    if (sscanf(date, "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31)
        return -1;
    long days = days_from_civil(y, (unsigned)m, (unsigned)d) + delta;
    long ny;
    unsigned nm, nd;
    civil_from_days(days, &ny, &nm, &nd);
    if (ny < 1 || ny > 9999)
        return -1;
    char buf[32];
    snprintf(buf, sizeof buf, "%04ld-%02u-%02u", ny, nm, nd);
    memcpy(out, buf, CT_DATE_LEN - 1);
    out[CT_DATE_LEN - 1] = '\0';
    return 0;
}

void date_today(char out[CT_DATE_LEN])
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(out, CT_DATE_LEN, "%Y-%m-%d", tm);
}
