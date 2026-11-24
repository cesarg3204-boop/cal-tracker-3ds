#ifndef CT_PEDOMETER_H
#define CT_PEDOMETER_H

/* The 3DS step counter (PTM service). It counts while the console is
 * closed or in sleep mode, and keeps an hourly history. */

#include <stdbool.h>
#include <stdint.h>

int pedometer_init(void);
void pedometer_exit(void);
bool pedometer_available(void);

/* Steps for each hour of `date` ("YYYY-MM-DD", console local time). For
 * today, only the hours so far are filled; the rest are 0. Writes the sum
 * to *total. Returns 0, or -1 if the counter can't be read. */
int pedometer_day(const char *date, uint16_t hours[24], unsigned *total);

#endif
