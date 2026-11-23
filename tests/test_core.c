/* Host-side tests for the portable core. Run with: make -f Makefile.tests */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/core/date.h"
#include "../source/core/food.h"
#include "../source/core/off.h"
#include "../source/core/storage.h"

static int failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                    \
        }                                                                  \
    } while (0)

#define NEAR(a, b) (fabsf((a) - (b)) < 0.01f)

static Food make_food(const char *name, float kcal, float p, float c, float f)
{
    Food food;
    memset(&food, 0, sizeof food);
    ct_strlcpy(food.name, name, sizeof food.name);
    food.kcal = kcal;
    food.protein = p;
    food.carbs = c;
    food.fat = f;
    return food;
}

static void test_totals(void)
{
    Entry e[2];
    e[0].food = make_food("Oats", 380, 13, 60, 7);
    e[0].grams = 50;
    e[0].meal = MEAL_BREAKFAST;
    e[1].food = make_food("Banana", 89, 1.1f, 23, 0.3f);
    e[1].grams = 120;
    e[1].meal = MEAL_SNACKS;

    Totals t = entry_totals(&e[0]);
    CHECK(NEAR(t.kcal, 190));
    CHECK(NEAR(t.protein, 6.5f));

    Totals all = sum_entries(e, 2, -1);
    CHECK(NEAR(all.kcal, 190 + 106.8f));
    Totals snacks = sum_entries(e, 2, MEAL_SNACKS);
    CHECK(NEAR(snacks.carbs, 27.6f));
    CHECK(strcmp(meal_name(MEAL_DINNER), "Dinner") == 0);
}

static void test_dates(void)
{
    char out[CT_DATE_LEN];
    CHECK(date_add_days("2026-11-23", 1, out) == 0 && strcmp(out, "2026-11-24") == 0);
    CHECK(date_add_days("2026-12-31", 1, out) == 0 && strcmp(out, "2027-01-01") == 0);
    CHECK(date_add_days("2024-03-01", -1, out) == 0 && strcmp(out, "2024-02-29") == 0);
    CHECK(date_add_days("2025-03-01", -1, out) == 0 && strcmp(out, "2025-02-28") == 0);
    CHECK(date_add_days("garbage", 1, out) == -1);
}

static void test_off(void)
{
    char url[512];
    CHECK(off_search_url(0, "peanut butter & jam", url, sizeof url) == 0);
    CHECK(strncmp(url, "https://search.openfoodfacts.org/search?q=peanut+butter+%26+jam&", 64) == 0);
    CHECK(off_search_url(1, "peanut butter & jam", url, sizeof url) == 0);
    CHECK(strstr(url, "search_terms=peanut+butter+%26+jam&") != NULL);
    CHECK(off_search_url(2, "x", url, sizeof url) == -1);

    const char *json =
        "{\"count\":3,\"products\":["
        "{\"product_name\":\"Greek Yogurt\",\"brands\":\"Fage,Total\",\"serving_quantity\":\"170\","
        " \"nutriments\":{\"energy-kcal_100g\":97,\"proteins_100g\":9,\"carbohydrates_100g\":4,\"fat_100g\":5}},"
        "{\"product_name\":\"\",\"nutriments\":{\"energy-kcal_100g\":1}},"
        "{\"product_name\":\"Rice\",\"nutriments\":{\"energy_100g\":1527,\"proteins_100g\":7}}"
        "]}";
    Food foods[5];
    int n = off_parse_search(json, foods, 5);
    CHECK(n == 2);
    CHECK(strcmp(foods[0].name, "Greek Yogurt") == 0);
    CHECK(strcmp(foods[0].brand, "Fage") == 0);
    CHECK(NEAR(foods[0].serving_g, 170));
    CHECK(NEAR(foods[0].protein, 9));
    CHECK(strcmp(foods[1].name, "Rice") == 0);
    CHECK(fabsf(foods[1].kcal - 365.0f) < 1.0f);
    CHECK(off_parse_search("not json", foods, 5) == -1);

    /* newer search service: "hits", brands as a list */
    const char *hits =
        "{\"hits\":[{\"product_name\":\"Oat Milk\",\"brands\":[\"Oatly\",\"Other\"],"
        " \"nutriments\":{\"energy-kcal_100g\":46,\"carbohydrates_100g\":6.7}}],\"count\":1}";
    n = off_parse_search(hits, foods, 5);
    CHECK(n == 1);
    CHECK(strcmp(foods[0].brand, "Oatly") == 0);
    CHECK(NEAR(foods[0].carbs, 6.7f));
}

static void test_storage(void)
{
    char dir[] = "/tmp/ct_test_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char base[300];
    snprintf(base, sizeof base, "%s/data/caltracker", dir);
    CHECK(storage_init(base) == 0);

    static DayLog day, loaded;
    CHECK(day_load("2026-11-23", &day) == 0 && day.count == 0);
    Entry e;
    e.food = make_food("Eggs", 143, 12.6f, 0.7f, 9.5f);
    e.grams = 100;
    e.meal = MEAL_BREAKFAST;
    CHECK(day_add(&day, &e) == 0);
    e.food = make_food("Chicken", 165, 31, 0, 3.6f);
    e.grams = 150;
    e.meal = MEAL_DINNER;
    CHECK(day_add(&day, &e) == 0);
    CHECK(day_save(&day) == 0);

    CHECK(day_load("2026-11-23", &loaded) == 0);
    CHECK(loaded.count == 2);
    CHECK(strcmp(loaded.entries[1].food.name, "Chicken") == 0);
    CHECK(loaded.entries[1].meal == MEAL_DINNER);
    CHECK(NEAR(loaded.entries[1].grams, 150));
    day_remove(&loaded, 0);
    CHECK(loaded.count == 1 && strcmp(loaded.entries[0].food.name, "Chicken") == 0);

    static FoodCache cache, cache2;
    CHECK(foods_load(&cache) == 0 && cache.count == 0);
    Food a = make_food("A", 1, 0, 0, 0), b = make_food("B", 2, 0, 0, 0);
    foods_touch(&cache, &a);
    foods_touch(&cache, &b);
    foods_touch(&cache, &a);
    CHECK(cache.count == 2 && strcmp(cache.items[0].name, "A") == 0);
    CHECK(foods_save(&cache) == 0);
    CHECK(foods_load(&cache2) == 0 && cache2.count == 2);
    CHECK(strcmp(cache2.items[1].name, "B") == 0);

    Targets t;
    CHECK(targets_load(&t) == 0 && NEAR(t.kcal, 2000));
    t.kcal = 2400;
    CHECK(targets_save(&t) == 0);
    Targets t2;
    CHECK(targets_load(&t2) == 0 && NEAR(t2.kcal, 2400) && NEAR(t2.fat, 70));
}

static void test_cache_full(void)
{
    static FoodCache cache;
    memset(&cache, 0, sizeof cache);
    char name[16];
    for (int i = 0; i < CT_MAX_FOODS + 5; i++) {
        snprintf(name, sizeof name, "F%d", i);
        Food f = make_food(name, 1, 0, 0, 0);
        foods_touch(&cache, &f);
    }
    CHECK(cache.count == CT_MAX_FOODS);
    snprintf(name, sizeof name, "F%d", CT_MAX_FOODS + 4);
    CHECK(strcmp(cache.items[0].name, name) == 0);
}

int main(void)
{
    test_totals();
    test_dates();
    test_off();
    test_storage();
    test_cache_full();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all core tests passed\n");
    return 0;
}
