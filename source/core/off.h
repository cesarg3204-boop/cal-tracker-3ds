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

/* Products of one brand. */
#define OFF_BRAND_FETCH 100
/* Brand endpoints, tried in order: exact text, fuzzy text (catches typos like
 * "goodless" for "Goodles"), then the older cgi brand filter. */
#define OFF_BRAND_ENDPOINTS 3
int off_brand_url(int endpoint, const char *brand, char *out, size_t size);

/* Case-insensitive (ASCII) substring test. */
int off_text_contains(const char *hay, const char *needle);
/* Like off_parse_search, but keeps only products whose brand matches `brand`
 * (contains it, ignoring case, or is within a small typo distance) and drops
 * duplicate names. */
int off_parse_brand(const char *json, const char *brand, Food *out, int max);

/* Looking up one product by its barcode (EAN-13, UPC-A, EAN-8). */
int off_product_url(const char *barcode, char *out, size_t size);
/* Returns 1 with the food filled, 0 if the product is unknown or has no
 * calories, or -1 if the reply is not JSON. */
int off_parse_product(const char *json, Food *out);

#endif
