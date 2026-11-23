#ifndef CT_OFF_H
#define CT_OFF_H

/* Open Food Facts: building search URLs and parsing the JSON response. */

#include "food.h"

#define OFF_PAGE_SIZE 20

/* Builds the search URL for a free-text query. Returns 0, or -1 if it does not fit. */
int off_search_url(const char *query, char *out, size_t size);

/* Parses a search response. Returns the number of foods written (<= max), or -1. */
int off_parse_search(const char *json, Food *out, int max);

#endif
