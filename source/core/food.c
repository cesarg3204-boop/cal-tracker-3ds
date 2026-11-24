#include "food.h"

#include <string.h>

static const char *MEAL_NAMES[MEAL_COUNT] = {"Breakfast", "Lunch", "Dinner", "Snacks"};

const char *meal_name(Meal meal)
{
    if (meal < 0 || meal >= MEAL_COUNT)
        return "?";
    return MEAL_NAMES[meal];
}

Totals entry_totals(const Entry *entry)
{
    float f = entry->grams / 100.0f;
    Totals t = {
        entry->food.kcal * f,
        entry->food.protein * f,
        entry->food.carbs * f,
        entry->food.fat * f,
    };
    return t;
}

Totals sum_entries(const Entry *entries, size_t count, int meal)
{
    Totals sum = {0, 0, 0, 0};
    for (size_t i = 0; i < count; i++) {
        if (meal >= 0 && (int)entries[i].meal != meal)
            continue;
        Totals t = entry_totals(&entries[i]);
        sum.kcal += t.kcal;
        sum.protein += t.protein;
        sum.carbs += t.carbs;
        sum.fat += t.fat;
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
