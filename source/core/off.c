#include "off.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/cJSON.h"

#define OFF_BASE "https://world.openfoodfacts.org/cgi/search.pl"
#define OFF_FIELDS "product_name,brands,nutriments,serving_quantity"

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

int off_search_url(const char *query, char *out, size_t size)
{
    char enc[256];
    if (url_encode(query, enc, sizeof enc) != 0)
        return -1;
    int n = snprintf(out, size,
                     OFF_BASE "?search_terms=%s&search_simple=1&action=process&json=1"
                              "&page_size=%d&fields=" OFF_FIELDS,
                     enc, OFF_PAGE_SIZE);
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

int off_parse_search(const char *json, Food *out, int max)
{
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return -1;
    const cJSON *products = cJSON_GetObjectItemCaseSensitive(root, "products");
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
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(p, "product_name");
        const cJSON *nutr = cJSON_GetObjectItemCaseSensitive(p, "nutriments");
        if (!cJSON_IsString(name) || !name->valuestring[0] || !cJSON_IsObject(nutr))
            continue;

        Food f;
        memset(&f, 0, sizeof f);
        if (!get_number(nutr, "energy-kcal_100g", &f.kcal)) {
            float kj;
            if (!get_number(nutr, "energy_100g", &kj))
                continue; /* no energy value: useless for a calorie tracker */
            f.kcal = kj / 4.184f;
        }
        get_number(nutr, "proteins_100g", &f.protein);
        get_number(nutr, "carbohydrates_100g", &f.carbs);
        get_number(nutr, "fat_100g", &f.fat);
        get_number(p, "serving_quantity", &f.serving_g);

        ct_strlcpy(f.name, name->valuestring, sizeof f.name);
        const cJSON *brands = cJSON_GetObjectItemCaseSensitive(p, "brands");
        if (cJSON_IsString(brands)) {
            /* keep only the first brand of a comma-separated list */
            ct_strlcpy(f.brand, brands->valuestring, sizeof f.brand);
            char *comma = strchr(f.brand, ',');
            if (comma)
                *comma = '\0';
        }
        out[n++] = f;
    }
    cJSON_Delete(root);
    return n;
}
