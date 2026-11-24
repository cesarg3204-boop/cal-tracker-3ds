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

/* Micronutrients. The first MICRO_KEY_COUNT show on the Today screen; the
 * rest are on the "See all micronutrients" page. */
typedef enum {
    MN_FIBER,
    MN_SUGAR,
    MN_SODIUM,
    MN_IRON,
    MN_ZINC,
    MN_SAT_FAT,
    MN_CHOLESTEROL,
    MN_CALCIUM,
    MN_MAGNESIUM,
    MN_POTASSIUM,
    MN_VIT_A,
    MN_VIT_C,
    MN_VIT_D,
    MN_VIT_B12,
    MICRO_COUNT
} Micro;
#define MICRO_KEY_COUNT 5

typedef struct {
    const char *name;    /* "Fiber" */
    const char *unit;    /* "g", "mg" or "mcg" */
    const char *json;    /* key in our own files */
    const char *off_key; /* Open Food Facts nutriment, always in grams */
    float from_grams;    /* 1, 1000 or 1000000 */
    int limit;           /* 1: a daily maximum (sugar, sodium...), not a goal */
} MicroInfo;

const MicroInfo *micro_info(Micro m);
/* Daily reference amount (US dietary reference intakes) for the person's
 * sex and age, in the micro's unit. */
float micro_target(Micro m, int female, int age);

/* Nutrition values are always stored per 100 g. */
typedef struct {
    char name[CT_NAME_LEN];
    char brand[CT_BRAND_LEN];
    float kcal;
    float protein;
    float carbs;
    float fat;
    float serving_g; /* suggested serving size, 0 if unknown */
    float micro[MICRO_COUNT]; /* per 100 g, in each micro's unit; 0 = unknown */
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
    float micro[MICRO_COUNT];
} Totals;

const char *meal_name(Meal meal);

/* Nutrients for one logged entry. */
Totals entry_totals(const Entry *entry);

/* Sum of all entries, or only one meal when meal >= 0. */
Totals sum_entries(const Entry *entries, size_t count, int meal);

/* Copy a string into a fixed buffer, always NUL-terminated. */
void ct_strlcpy(char *dst, const char *src, size_t size);

#endif
