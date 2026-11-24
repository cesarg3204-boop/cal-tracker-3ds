#ifndef CT_FOOD_H
#define CT_FOOD_H

#include <stddef.h>

#define CT_NAME_LEN 64
#define CT_BRAND_LEN 40

typedef enum {
    MEAL_BREAKFAST = 0,
    MEAL_LUNCH,
    MEAL_DINNER,
    MEAL_SNACKS,
    MEAL_COUNT
} Meal;

/* Nutrition values are always stored per 100 g. */
typedef struct {
    char name[CT_NAME_LEN];
    char brand[CT_BRAND_LEN];
    float kcal;
    float protein;
    float carbs;
    float fat;
    float serving_g; /* suggested serving size, 0 if unknown */
} Food;

typedef struct {
    Food food;
    float grams;
    Meal meal;
    int quick; /* quick entry: food holds the totals and grams is 100 */
} Entry;

typedef struct {
    float kcal;
    float protein;
    float carbs;
    float fat;
} Totals;

const char *meal_name(Meal meal);

/* Nutrients for one logged entry. */
Totals entry_totals(const Entry *entry);

/* Sum of all entries, or only one meal when meal >= 0. */
Totals sum_entries(const Entry *entries, size_t count, int meal);

/* Copy a string into a fixed buffer, always NUL-terminated. */
void ct_strlcpy(char *dst, const char *src, size_t size);

#endif
