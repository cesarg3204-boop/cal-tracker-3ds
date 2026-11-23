#ifndef CT_OFF_H
#define CT_OFF_H

/* Open Food Facts: building search URLs and parsing the JSON response. */

#include "food.h"

#define OFF_PAGE_SIZE 20

/* Search endpoints, tried in order: the current search service first, then the
 * older cgi search (which is rate limited and often answers 503). */
#define OFF_ENDPOINT_COUNT 2

/* Builds the search URL for a free-text query on endpoint 0..OFF_ENDPOINT_COUNT-1.
 * Returns 0, or -1 if it does not fit. */
int off_search_url(int endpoint, const char *query, char *out, size_t size);

/* Parses a search response from either endpoint ("hits" or "products").
 * Returns the number of foods written (<= max), or -1. */
int off_parse_search(const char *json, Food *out, int max);

#endif
