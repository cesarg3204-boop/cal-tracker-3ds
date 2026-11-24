#include "food.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *MEAL_NAMES[MEAL_COUNT] = {"Breakfast", "Lunch", "Dinner", "Snacks"};

float unit_grams(const Food *food, Unit unit)
{
    switch (unit) {
    case UNIT_GRAMS: return 1.0f;
    case UNIT_SERVINGS: return food->serving_g > 0 ? food->serving_g : 0;
    case UNIT_OUNCES: return 28.3495f;
    case UNIT_ML: return 1.0f;
    case UNIT_CUPS: return 240.0f;
    default: return 0;
    }
}

/* "1 bar" -> "bar", "" -> "serving" */
static void serving_noun(const Food *food, char *out, size_t size)
{
    const char *l = food->serving_label;
    if (l[0] == '1' && l[1] == ' ')
        l += 2;
    if (!l[0] || (l[0] >= '0' && l[0] <= '9'))
        l = "serving";
    ct_strlcpy(out, l, size);
}

void unit_choice_text(const Food *food, Unit unit, char *out, size_t size)
{
    switch (unit) {
    case UNIT_SERVINGS:
        if (food->serving_label[0])
            snprintf(out, size, "Servings (%s = %.0f g)", food->serving_label, food->serving_g);
        else
            snprintf(out, size, "Servings (1 serving = %.0f g)", food->serving_g);
        break;
    case UNIT_GRAMS: snprintf(out, size, "Grams"); break;
    case UNIT_OUNCES: snprintf(out, size, "Ounces (1 oz = 28 g)"); break;
    case UNIT_ML: snprintf(out, size, "Milliliters (drinks)"); break;
    case UNIT_CUPS: snprintf(out, size, "Cups (drinks, 240 ml)"); break;
    default: out[0] = '\0'; break;
    }
}

static void number_text(float v, char *out, size_t size)
{
    float r = (float)(int)(v + 0.5f);
    if (v - r < 0.05f && r - v < 0.05f)
        snprintf(out, size, "%.0f", r);
    else
        snprintf(out, size, "%.1f", v);
}

void amount_text(const Entry *e, char *out, size_t size)
{
    char n[24];
    if (e->unit == UNIT_GRAMS || e->amount <= 0) {
        snprintf(out, size, "%.0f g", e->grams);
        return;
    }
    number_text(e->amount, n, sizeof n);
    switch (e->unit) {
    case UNIT_SERVINGS: {
        char noun[CT_SERVING_LEN];
        serving_noun(&e->food, noun, sizeof noun);
        size_t len = strlen(noun);
        bool one = e->amount > 0.95f && e->amount < 1.05f;
        bool plural = !one && len > 0 && noun[len - 1] != 's';
        snprintf(out, size, "%s %s%s", n, noun, plural ? "s" : "");
        break;
    }
    case UNIT_OUNCES: snprintf(out, size, "%s oz", n); break;
    case UNIT_ML: snprintf(out, size, "%s ml", n); break;
    case UNIT_CUPS: snprintf(out, size, "%s cup%s", n, e->amount > 0.95f && e->amount < 1.05f ? "" : "s"); break;
    default: snprintf(out, size, "%.0f g", e->grams); break;
    }
}

/* Reads "60 g", "60g", "250 ml" or "1,5 g" at p. Returns 1 and the value. */
static int weight_at(const char *p, float *value, const char **end)
{
    char buf[16];
    size_t k = 0, n = 0;
    while ((p[k] >= '0' && p[k] <= '9') || p[k] == '.' || p[k] == ',') {
        if (n + 1 < sizeof buf)
            buf[n++] = p[k] == ',' ? '.' : p[k];
        k++;
    }
    if (k == 0)
        return 0;
    buf[n] = '\0';
    const char *u = p + k;
    while (*u == ' ')
        u++;
    int len = 0;
    if (u[0] == 'g' || u[0] == 'G')
        len = 1;
    else if ((u[0] == 'm' || u[0] == 'M') && (u[1] == 'l' || u[1] == 'L'))
        len = 2;
    if (!len || ((u[len] | 0x20) >= 'a' && (u[len] | 0x20) <= 'z'))
        return 0; /* "1 granola bar" is not a weight */
    *value = (float)atof(buf);
    if (end)
        *end = u + len;
    return *value > 0;
}

/* Copies text[0..n) trimmed. A piece that is only a weight is not a label. */
static void take_label(const char *text, size_t n, char *label, size_t size)
{
    while (n > 0 && (*text == ' '))
        text++, n--;
    while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == ')'))
        n--;
    if (n == 0 || n >= size)
        return;
    float v;
    const char *end;
    if (weight_at(text, &v, &end) && (size_t)(end - text) >= n)
        return;
    memcpy(label, text, n);
    label[n] = '\0';
}

int serving_parse(const char *text, char *label, size_t label_size, float *grams)
{
    label[0] = '\0';
    if (!text || !text[0])
        return 0;
    int found = 0;
    for (const char *p = text; *p && !found; p++) {
        bool start = p == text || !((p[-1] >= '0' && p[-1] <= '9') || p[-1] == '.' || p[-1] == ',');
        float v;
        if (start && weight_at(p, &v, NULL)) {
            *grams = v;
            found = 1;
        }
    }
    const char *paren = strchr(text, '(');
    if (paren) {
        take_label(text, (size_t)(paren - text), label, label_size);
        if (!label[0])
            take_label(paren + 1, strlen(paren + 1), label, label_size);
    } else {
        take_label(text, strlen(text), label, label_size);
    }
    return found || label[0];
}

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
