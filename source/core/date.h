#ifndef CT_DATE_H
#define CT_DATE_H

/* Dates are plain "YYYY-MM-DD" strings (11 bytes with the NUL). */
#define CT_DATE_LEN 11

/* Returns 0 on success, -1 if the input is not a valid date. */
int date_add_days(const char *date, int delta, char out[CT_DATE_LEN]);

/* Days since 1970-01-01 (negative before). Returns 0, or -1 if invalid. */
int date_to_days(const char *date, long *days);

/* Today's local date from the system clock. */
void date_today(char out[CT_DATE_LEN]);

#endif
