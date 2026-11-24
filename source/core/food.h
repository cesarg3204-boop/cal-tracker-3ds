#ifndef CT_FOOD_H
#define CT_FOOD_H

#include <stddef.h>

#define CT_NAME_LEN 64
#define CT_BRAND_LEN 40
#define CT_SERVING_LEN 24

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
    char serving_label[CT_SERVING_LEN]; /* "1 bar", "2 cookies"; "" if unknown */
    float micro[MICRO_COUNT]; /* per 100 g, in each micro's unit; 0 = unknown */
} Food;

/* Ways to say how much was eaten. Everything is stored in grams too. */
typedef enum { UNIT_GRAMS, UNIT_SERVINGS, UNIT_OUNCES, UNIT_ML, UNIT_CUPS, UNIT_COUNT } Unit;

typedef struct {
    Food food;
    float grams;
    Meal meal;
    int quick; /* quick entry: food holds the totals and grams is 100 */
    Unit unit;    /* how the amount was typed in */
    float amount; /* in that unit (same as grams for UNIT_GRAMS) */
} Entry;

typedef struct {
    float kcal;
    float protein;
    float carbs;
    float fat;
    float micro[MICRO_COUNT];
} Totals;

const char *meal_name(Meal meal);

/* Grams in one of `unit` for this food, or 0 when it can't be used
 * (servings need a known serving size). ml and cups count 1 ml as 1 g,
 * which is right for drinks like milk or juice. */
float unit_grams(const Food *food, Unit unit);
/* "Servings (1 bar = 60 g)", "Grams", "Ounces (28 g)"... for pickers. */
void unit_choice_text(const Food *food, Unit unit, char *out, size_t size);
/* How an entry's amount reads in a list: "1.5 bars", "2 oz", "120 g". */
void amount_text(const Entry *e, char *out, size_t size);
/* Reads Open Food Facts' serving_size text ("1 bar (60 g)", "30 g") into a
 * label ("1 bar", or "" when it only gives a weight) and, if found, grams.
 * Returns 1 if anything useful was found. */
int serving_parse(const char *text, char *label, size_t label_size, float *grams);

/* Nutrients for one logged entry. */
Totals entry_totals(const Entry *entry);

/* Sum of all entries, or only one meal when meal >= 0. */
Totals sum_entries(const Entry *entries, size_t count, int meal);

/* Copy a string into a fixed buffer, always NUL-terminated. */
void ct_strlcpy(char *dst, const char *src, size_t size);

#endif
