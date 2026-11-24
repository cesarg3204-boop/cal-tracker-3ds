#include "food.h"

#include <string.h>

static const char *MEAL_NAMES[MEAL_COUNT] = {"Breakfast", "Lunch", "Dinner", "Snacks"};

const char *meal_name(Meal meal)
{
    if (meal < 0 || meal >= MEAL_COUNT)
        return "?";
    return MEAL_NAMES[meal];
}

static const MicroInfo MICROS[MICRO_COUNT] = {
    {"Fiber", "g", "fiber", "fiber_100g", 1, 0},
    {"Sugar", "g", "sugar", "sugars_100g", 1, 1},
    {"Sodium", "mg", "sodium", "sodium_100g", 1000, 1},
    {"Iron", "mg", "iron", "iron_100g", 1000, 0},
    {"Zinc", "mg", "zinc", "zinc_100g", 1000, 0},
    {"Saturated fat", "g", "sat_fat", "saturated-fat_100g", 1, 1},
    {"Cholesterol", "mg", "cholesterol", "cholesterol_100g", 1000, 1},
    {"Calcium", "mg", "calcium", "calcium_100g", 1000, 0},
    {"Magnesium", "mg", "magnesium", "magnesium_100g", 1000, 0},
    {"Potassium", "mg", "potassium", "potassium_100g", 1000, 0},
    {"Vitamin A", "mcg", "vit_a", "vitamin-a_100g", 1000000, 0},
    {"Vitamin C", "mg", "vit_c", "vitamin-c_100g", 1000, 0},
    {"Vitamin D", "mcg", "vit_d", "vitamin-d_100g", 1000000, 0},
    {"Vitamin B12", "mcg", "vit_b12", "vitamin-b12_100g", 1000000, 0},
};

const MicroInfo *micro_info(Micro m)
{
    return (m >= 0 && m < MICRO_COUNT) ? &MICROS[m] : &MICROS[0];
}

float micro_target(Micro m, int female, int age)
{
    int teen = age > 0 && age < 19;
    switch (m) {
    case MN_FIBER: return teen ? (female ? 26 : 31) : (female ? 25 : 38);
    case MN_SUGAR: return 50; /* WHO: under 10% of 2000 kcal */
    case MN_SODIUM: return 2300;
    case MN_IRON: return teen ? (female ? 15 : 11) : (female ? 18 : 8);
    case MN_ZINC: return teen ? (female ? 9 : 11) : (female ? 8 : 11);
    case MN_SAT_FAT: return 22;
    case MN_CHOLESTEROL: return 300;
    case MN_CALCIUM: return teen ? 1300 : 1000;
    case MN_MAGNESIUM: return teen ? (female ? 360 : 410) : (female ? 310 : 400);
    case MN_POTASSIUM: return teen ? (female ? 2300 : 3000) : (female ? 2600 : 3400);
    case MN_VIT_A: return female ? 700 : 900;
    case MN_VIT_C: return teen ? (female ? 65 : 75) : (female ? 75 : 90);
    case MN_VIT_D: return 15;
    case MN_VIT_B12: return 2.4f;
    default: return 0;
    }
}

Totals entry_totals(const Entry *entry)
{
    float f = entry->grams / 100.0f;
    Totals t;
    memset(&t, 0, sizeof t);
    t.kcal = entry->food.kcal * f;
    t.protein = entry->food.protein * f;
    t.carbs = entry->food.carbs * f;
    t.fat = entry->food.fat * f;
    for (int i = 0; i < MICRO_COUNT; i++)
        t.micro[i] = entry->food.micro[i] * f;
    return t;
}

Totals sum_entries(const Entry *entries, size_t count, int meal)
{
    Totals sum;
    memset(&sum, 0, sizeof sum);
    for (size_t i = 0; i < count; i++) {
        if (meal >= 0 && (int)entries[i].meal != meal)
            continue;
        Totals t = entry_totals(&entries[i]);
        sum.kcal += t.kcal;
        sum.protein += t.protein;
        sum.carbs += t.carbs;
        sum.fat += t.fat;
        for (int k = 0; k < MICRO_COUNT; k++)
            sum.micro[k] += t.micro[k];
    }
    return sum;
}

void ct_strlcpy(char *dst, const char *src, size_t size)
{
    if (size == 0)
        return;
    if (!src)
        src = "";
    size_t n = strlen(src);
    if (n >= size)
        n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}
