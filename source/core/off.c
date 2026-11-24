#include "off.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/cJSON.h"

#define OFF_SEARCH "https://search.openfoodfacts.org/search"
#define OFF_CGI "https://world.openfoodfacts.org/cgi/search.pl"
#define OFF_FIELDS "product_name,product_name_es,product_name_en,brands,nutriments,serving_quantity"
/* Ask for more than we show, because names in non-Latin scripts are dropped. */
#define OFF_FETCH_SIZE (OFF_PAGE_SIZE * 2)

static int url_encode(const char *in, char *out, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        unsigned char c = *p;
        int plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (plain) {
            if (o + 1 >= size)
                return -1;
            out[o++] = (char)c;
        } else if (c == ' ') {
            if (o + 1 >= size)
                return -1;
            out[o++] = '+';
        } else {
            if (o + 3 >= size)
                return -1;
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    if (o >= size)
        return -1;
    out[o] = '\0';
    return 0;
}

int off_search_url(int endpoint, const char *query, char *out, size_t size)
{
    char enc[256];
    if (url_encode(query, enc, sizeof enc) != 0)
        return -1;
    int n;
    if (endpoint == 0)
        n = snprintf(out, size, OFF_SEARCH "?q=%s&langs=es,en&page_size=%d&fields=" OFF_FIELDS,
                     enc, OFF_FETCH_SIZE);
    else if (endpoint == 1)
        n = snprintf(out, size,
                     OFF_CGI "?search_terms=%s&search_simple=1&action=process&json=1"
                             "&page_size=%d&fields=" OFF_FIELDS,
                     enc, OFF_FETCH_SIZE);
    else
        return -1;
    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

int off_brand_url(int endpoint, const char *brand, char *out, size_t size)
{
    char enc[128];
    if (url_encode(brand, enc, sizeof enc) != 0)
        return -1;
    int n;
    if (endpoint == 0 || endpoint == 1)
        n = snprintf(out, size, OFF_SEARCH "?q=%s%s&langs=es,en&page_size=%d&fields=" OFF_FIELDS,
                     enc, endpoint == 1 ? "~" : "", OFF_BRAND_FETCH);
    else if (endpoint == 2)
        n = snprintf(out, size,
                     OFF_CGI "?action=process&tagtype_0=brands&tag_contains_0=contains&tag_0=%s"
                             "&json=1&page_size=%d&fields=" OFF_FIELDS,
                     enc, OFF_BRAND_FETCH);
    else
        return -1;
    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

static int get_number(const cJSON *obj, const char *key, float *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(v)) {
        *out = (float)v->valuedouble;
        return 1;
    }
    if (cJSON_IsString(v) && v->valuestring[0]) {
        char *end;
        float f = strtof(v->valuestring, &end);
        if (end != v->valuestring) {
            *out = f;
            return 1;
        }
    }
    return 0;
}

/* True if every character is Latin (ASCII, accents, Latin Extended): U+0000..U+024F.
 * Names in Cyrillic, Greek, Arabic, CJK and so on are skipped, since the user
 * cannot read them. */
static int is_latin(const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x80)
            continue;
        /* two-byte sequences with a lead byte up to 0xC9 encode U+0080..U+027F */
        if (*p >= 0xC2 && *p <= 0xC9 && (p[1] & 0xC0) == 0x80) {
            p++;
            continue;
        }
        /* allow typographic punctuation such as the apostrophe U+2019 (E2 80 xx) */
        if (*p == 0xE2 && p[1] == 0x80 && (p[2] & 0xC0) == 0x80) {
            p += 2;
            continue;
        }
        return 0;
    }
    return 1;
}

/* First non-empty string among the given keys. */
static const char *first_name(const cJSON *p)
{
    static const char *keys[] = {"product_name_es", "product_name_en", "product_name"};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(p, keys[i]);
        if (cJSON_IsString(v) && v->valuestring[0])
            return v->valuestring;
    }
    return NULL;
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* Case-insensitive (ASCII) substring test. */
int off_text_contains(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (n == 0)
        return 1;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && lower(hay[i]) == lower(needle[i]))
            i++;
        if (i == n)
            return 1;
    }
    return 0;
}

#define contains_ci off_text_contains

/* Edit distance between two short strings, ignoring case (capped inputs). */
static int edit_distance(const char *a, const char *b)
{
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la > 40 || lb > 40)
        return 99;
    int prev[41], cur[41];
    for (int j = 0; j <= lb; j++)
        prev[j] = j;
    for (int i = 1; i <= la; i++) {
        cur[0] = i;
        for (int j = 1; j <= lb; j++) {
            int cost = lower(a[i - 1]) == lower(b[j - 1]) ? 0 : 1;
            int best = prev[j - 1] + cost;
            if (prev[j] + 1 < best) best = prev[j] + 1;
            if (cur[j - 1] + 1 < best) best = cur[j - 1] + 1;
            cur[j] = best;
        }
        memcpy(prev, cur, sizeof(int) * (size_t)(lb + 1));
    }
    return prev[lb];
}

/* One brand name against the wanted brand: contains it, or a near typo. */
static int brand_matches(const char *have, const char *want)
{
    if (contains_ci(have, want))
        return 1;
    /* compare each comma-separated part on its own */
    char part[64];
    const char *p = have;
    while (*p) {
        size_t n = strcspn(p, ",");
        if (n >= sizeof part)
            n = sizeof part - 1;
        memcpy(part, p, n);
        part[n] = '\0';
        char *s = part;
        while (*s == ' ')
            s++;
        size_t wl = strlen(want);
        int allowed = wl >= 8 ? 2 : (wl >= 4 ? 1 : 0);
        if (allowed && edit_distance(s, want) <= allowed)
            return 1;
        p += strcspn(p, ",");
        if (*p == ',')
            p++;
    }
    return 0;
}

/* Does the product's "brands" (a string or a list) match the brand? */
static int has_brand(const cJSON *brands, const char *brand)
{
    if (cJSON_IsString(brands))
        return brand_matches(brands->valuestring, brand);
    const cJSON *b;
    cJSON_ArrayForEach(b, brands)
    {
        if (cJSON_IsString(b) && brand_matches(b->valuestring, brand))
            return 1;
    }
    return 0;
}

static int parse_products(const char *json, const char *brand_filter, Food *out, int max);

static void read_micros(const cJSON *nutr, Food *f)
{
    for (int i = 0; i < MICRO_COUNT; i++) {
        const MicroInfo *mi = micro_info((Micro)i);
        float g;
        if (get_number(nutr, mi->off_key, &g) && g > 0)
            f->micro[i] = g * mi->from_grams;
    }
    float salt;
    if (f->micro[MN_SODIUM] <= 0 && get_number(nutr, "salt_100g", &salt) && salt > 0)
        f->micro[MN_SODIUM] = salt / 2.5f * 1000.0f; /* salt is 40% sodium */
}

int off_parse_search(const char *json, Food *out, int max)
{
    return parse_products(json, NULL, out, max);
}

int off_parse_brand(const char *json, const char *brand, Food *out, int max)
{
    return parse_products(json, brand, out, max);
}

/* Fills one food from a product object. Returns 0 if it has no calories. */
static int parse_one(const cJSON *p, const char *name, const cJSON *nutr, Food *f)
{
    memset(f, 0, sizeof *f);
    if (!get_number(nutr, "energy-kcal_100g", &f->kcal)) {
        float kj;
        if (!get_number(nutr, "energy_100g", &kj))
            return 0; /* no energy value: useless for a calorie tracker */
        f->kcal = kj / 4.184f;
    }
    get_number(nutr, "proteins_100g", &f->protein);
    get_number(nutr, "carbohydrates_100g", &f->carbs);
    get_number(nutr, "fat_100g", &f->fat);
    get_number(p, "serving_quantity", &f->serving_g);
    read_micros(nutr, f);

    ct_strlcpy(f->name, name, sizeof f->name);
    const cJSON *brands = cJSON_GetObjectItemCaseSensitive(p, "brands");
    if (cJSON_IsArray(brands))
        brands = cJSON_GetArrayItem(brands, 0); /* newer API: a list of brands */
    if (cJSON_IsString(brands)) {
        /* keep only the first brand of a comma-separated list */
        ct_strlcpy(f->brand, brands->valuestring, sizeof f->brand);
        char *comma = strchr(f->brand, ',');
        if (comma)
            *comma = '\0';
    }
    return 1;
}

static int parse_products(const char *json, const char *brand_filter, Food *out, int max)
{
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return -1;
    const cJSON *products = cJSON_GetObjectItemCaseSensitive(root, "hits");
    if (!cJSON_IsArray(products))
        products = cJSON_GetObjectItemCaseSensitive(root, "products");
    if (!cJSON_IsArray(products)) {
        cJSON_Delete(root);
        return -1;
    }

    int n = 0;
    const cJSON *p;
    cJSON_ArrayForEach(p, products)
    {
        if (n >= max)
            break;
        const char *name = first_name(p);
        const cJSON *nutr = cJSON_GetObjectItemCaseSensitive(p, "nutriments");
        if (!name || !is_latin(name) || !cJSON_IsObject(nutr))
            continue;
        if (brand_filter) {
            if (!has_brand(cJSON_GetObjectItemCaseSensitive(p, "brands"), brand_filter))
                continue;
            int dup = 0;
            for (int k = 0; k < n && !dup; k++)
                dup = contains_ci(out[k].name, name) && strlen(out[k].name) == strlen(name);
            if (dup)
                continue;
        }

        Food f;
        if (!parse_one(p, name, nutr, &f))
            continue;
        out[n++] = f;
    }
    cJSON_Delete(root);
    return n;
}

int off_product_url(const char *barcode, char *out, size_t size)
{
    size_t len = strlen(barcode);
    if (len < 8 || len > 14)
        return -1;
    for (size_t i = 0; i < len; i++)
        if (barcode[i] < '0' || barcode[i] > '9')
            return -1;
    int n = snprintf(out, size, "https://world.openfoodfacts.org/api/v2/product/%s?fields=" OFF_FIELDS, barcode);
    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

int off_parse_product(const char *json, Food *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return -1;
    int rc = 0;
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(root, "product");
    if (cJSON_IsObject(p)) {
        const char *name = first_name(p);
        const cJSON *nutr = cJSON_GetObjectItemCaseSensitive(p, "nutriments");
        if (!name)
            name = "Scanned product";
        if (cJSON_IsObject(nutr) && parse_one(p, name, nutr, out))
            rc = 1;
    }
    cJSON_Delete(root);
    return rc;
}
