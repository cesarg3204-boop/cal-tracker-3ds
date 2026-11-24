/* Host-side tests for the portable core. Run with: make -f Makefile.tests */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/core/activity.h"
#include "../source/core/date.h"
#include "../source/core/food.h"
#include "../source/core/off.h"
#include "../source/core/profile.h"
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

    /* Spanish name preferred; Cyrillic-only names dropped; accents kept */
    const char *mixed =
        "{\"hits\":["
        "{\"product_name\":\"\u0421\u043e\u0443\u0441 \u0441\u044b\u0440\u043d\u044b\u0439\",\"nutriments\":{\"energy-kcal_100g\":92}},"
        "{\"product_name\":\"Fries\",\"product_name_es\":\"Papas fritas\",\"nutriments\":{\"energy-kcal_100g\":337}},"
        "{\"product_name\":\"Caf\u00e9 cr\u00e8me McDonald\u2019s\",\"nutriments\":{\"energy-kcal_100g\":40}}"
        "]}";
    n = off_parse_search(mixed, foods, 5);
    CHECK(n == 2);
    CHECK(strcmp(foods[0].name, "Papas fritas") == 0);
    CHECK(strncmp(foods[1].name, "Caf", 3) == 0);

    /* brand pages: keep only that brand, drop duplicate names */
    CHECK(off_brand_url(0, "Quest", url, sizeof url) == 0 && strstr(url, "q=Quest&") != NULL);
    CHECK(off_brand_url(1, "Quest", url, sizeof url) == 0 && strstr(url, "q=Quest~&") != NULL);
    CHECK(off_brand_url(2, "Quest", url, sizeof url) == 0 && strstr(url, "tag_0=Quest&") != NULL);
    CHECK(off_brand_url(3, "Quest", url, sizeof url) == -1);
    const char *brand_json =
        "{\"hits\":["
        "{\"product_name\":\"Cookies & Cream Bar\",\"brands\":[\"Quest Nutrition\"],\"nutriments\":{\"energy-kcal_100g\":350}},"
        "{\"product_name\":\"Quest for Fire Chips\",\"brands\":\"Other Co\",\"nutriments\":{\"energy-kcal_100g\":500}},"
        "{\"product_name\":\"Cookies & cream bar\",\"brands\":\"Quest\",\"nutriments\":{\"energy-kcal_100g\":351}},"
        "{\"product_name\":\"Tortilla Style Chips\",\"brands\":\"quest\",\"nutriments\":{\"energy-kcal_100g\":470}}"
        "]}";
    n = off_parse_brand(brand_json, "Quest", foods, 5);
    CHECK(n == 2);
    CHECK(strcmp(foods[0].name, "Cookies & Cream Bar") == 0);
    CHECK(strcmp(foods[1].name, "Tortilla Style Chips") == 0);

    /* a typo in the brand still matches ("goodless" -> "Goodles"), others don't */
    const char *typo_json =
        "{\"hits\":["
        "{\"product_name\":\"Cheddy Mac\",\"brands\":\"Goodles\",\"nutriments\":{\"energy-kcal_100g\":380}},"
        "{\"product_name\":\"Goodness Bar\",\"brands\":\"Nature Valley\",\"nutriments\":{\"energy-kcal_100g\":450}}"
        "]}";
    n = off_parse_brand(typo_json, "goodless", foods, 5);
    CHECK(n == 1 && strcmp(foods[0].brand, "Goodles") == 0);
    CHECK(off_text_contains("Quest Nutrition", "NUTRI") && !off_text_contains("Quest", "Barebells"));
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
    memset(&e, 0, sizeof e);
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

    /* quick entries keep their flag */
    e.food = make_food("Abuela's arroz con pollo", 650, 35, 80, 18);
    e.grams = 100;
    e.meal = MEAL_LUNCH;
    e.quick = 1;
    CHECK(day_add(&loaded, &e) == 0 && day_save(&loaded) == 0);
    CHECK(day_load("2026-11-23", &day) == 0 && day.count == 2 && day.entries[1].quick == 1);
    CHECK(day.entries[0].quick == 0);
    CHECK(NEAR(sum_entries(day.entries, day.count, MEAL_LUNCH).kcal, 650));

    /* favorites */
    static FavList favs, favs2;
    CHECK(favs_load(&favs) == 0 && favs.count == 0);
    Food bar = make_food("Protein Bar", 350, 30, 40, 8);
    ct_strlcpy(bar.brand, "Quest", sizeof bar.brand);
    CHECK(favs_toggle(&favs, &bar, 60) == 1);
    CHECK(favs_toggle(&favs, &a, 0) == 1);
    CHECK(favs_find(&favs, &bar) == 0 && NEAR(favs.items[1].grams, 100));
    CHECK(favs_save(&favs) == 0 && favs_load(&favs2) == 0 && favs2.count == 2);
    CHECK(NEAR(favs2.items[0].grams, 60) && strcmp(favs2.items[0].food.brand, "Quest") == 0);
    CHECK(favs_toggle(&favs2, &bar, 0) == 0 && favs2.count == 1 && favs_find(&favs2, &bar) == -1);

    /* saved meals */
    static MealList meals, meals2;
    static SavedMeal m;
    memset(&m, 0, sizeof m);
    ct_strlcpy(m.name, "My usual breakfast", sizeof m.name);
    m.items[0] = day.entries[0];
    m.items[1] = day.entries[1];
    m.count = 2;
    CHECK(meals_load(&meals) == 0 && meals.count == 0);
    CHECK(meals_add(&meals, &m) == 0 && meals_save(&meals) == 0);
    CHECK(meals_load(&meals2) == 0 && meals2.count == 1 && meals2.items[0].count == 2);
    CHECK(strcmp(meals2.items[0].name, "My usual breakfast") == 0);
    CHECK(meals2.items[0].items[1].quick == 1);
    meals_remove(&meals2, 0);
    CHECK(meals2.count == 0);

    /* brands and their cached products */
    static BrandList brands, brands2;
    CHECK(brands_load(&brands) == 0 && brands.count == 0);
    CHECK(brands_add(&brands, "Quest") == 1 && brands_add(&brands, "Barebells") == 1);
    CHECK(brands_add(&brands, "quest") == 0 && brands.count == 2);
    CHECK(brands_save(&brands) == 0 && brands_load(&brands2) == 0 && brands2.count == 2);
    brands_remove(&brands2, 0);
    CHECK(brands2.count == 1 && strcmp(brands2.names[0], "Barebells") == 0);
    Food cached[4];
    CHECK(brand_cache_load("Quest Nutrition", cached, 4) == -1);
    CHECK(brand_cache_save("Quest Nutrition", &bar, 1) == 0);
    CHECK(brand_cache_load("Quest Nutrition", cached, 4) == 1 && strcmp(cached[0].name, "Protein Bar") == 0);
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

static void test_profile(void)
{
    UserProfile p;
    memset(&p, 0, sizeof p);
    p.age = 14;
    p.sex = SEX_MALE;
    p.height_cm = 170;
    p.body_fat = 15;
    /* Matches Cronometer: 10*53 + 6.25*170 - 5*14 + 5 = 1527.5 */
    CHECK(fabsf(bmr(&p, 53) - 1527.5f) < 0.1f);
    CHECK(fabsf(bmi(53, 170) - 18.34f) < 0.01f);
    CHECK(bmi(0, 170) == 0 && bmr(&p, 0) == 0);
    p.sex = SEX_FEMALE;
    CHECK(fabsf(bmr(&p, 53) - 1361.5f) < 0.1f);

    char dir[] = "/tmp/ct_prof_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    CHECK(storage_init(dir) == 0);
    UserProfile q;
    CHECK(profile_load(&q) == 0 && q.age == 0);
    CHECK(profile_save(&p) == 0);
    CHECK(profile_load(&q) == 0);
    CHECK(q.age == 14 && q.sex == SEX_FEMALE && NEAR(q.height_cm, 170) && NEAR(q.body_fat, 15));

    static Series log, log2;
    CHECK(series_load(&log, WEIGHTS_FILE) == 0 && log.count == 0);
    CHECK(series_latest(&log, NULL) == 0);
    series_set(&log, "2026-11-20", 53.0f);
    series_set(&log, "2026-11-10", 52.0f);
    series_set(&log, "2026-11-15", 52.5f);
    series_set(&log, "2026-11-15", 52.6f);
    CHECK(log.count == 3 && strcmp(log.items[0].date, "2026-11-10") == 0);
    CHECK(NEAR(log.items[1].value, 52.6f));
    CHECK(NEAR(series_latest(&log, NULL), 53.0f));
    CHECK(NEAR(series_latest(&log, "2026-11-16"), 52.6f));
    CHECK(series_latest(&log, "2026-11-01") == 0);
    CHECK(NEAR(series_get(&log, "2026-11-10"), 52.0f) && series_get(&log, "2026-11-11") == 0);
    CHECK(series_save(&log, WEIGHTS_FILE) == 0);
    CHECK(series_load(&log2, WEIGHTS_FILE) == 0 && log2.count == 3 && NEAR(log2.items[2].value, 53.0f));
    series_set(&log2, "2026-11-15", 0);
    CHECK(log2.count == 2 && series_get(&log2, "2026-11-15") == 0);
    CHECK(series_load(&log, STEPS_FILE) == 0 && log.count == 0);
}

static void test_activity(void)
{
    long days;
    CHECK(date_to_days("1970-01-02", &days) == 0 && days == 1);
    CHECK(date_to_days("nope", &days) == -1);
    long long ms;
    CHECK(date_pedometer_ms("2000-01-01", &ms) == 0 && ms == 0);
    CHECK(date_pedometer_ms("2000-01-02", &ms) == 0 && ms == 86400000LL);

    /* 10,000 steps at 170 cm: 70.4 cm stride, 7.04 km, 0.5 * 53 * 7.04 = 186.6 */
    CHECK(fabsf(steps_kcal(10000, 53, 170) - 186.56f) < 0.1f);
    CHECK(fabsf(steps_kcal(1000, 70, 0) - 24.5f) < 0.01f); /* default 70 cm stride */
    CHECK(steps_kcal(5000, 0, 170) == 0);

    /* an hour of weights (MET 5) at 53 kg: (5 - 1) * 53 = 212 */
    CHECK(NEAR(workout_kcal(5.0f, 53, 60), 212.0f));
    CHECK(NEAR(workout_kcal(9.8f, 53, 30), 233.2f));
    CHECK(workout_kcal(5.0f, 0, 60) == 0 && workout_kcal(0, 53, 60) == 0);
    CHECK(WORKOUT_TYPE_COUNT > 3 && WORKOUT_TYPES[WORKOUT_TYPE_COUNT - 1].met == 0);

    char dir[] = "/tmp/ct_act_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    CHECK(storage_init(dir) == 0);
    WorkoutDay day, again;
    CHECK(workouts_load("2026-11-23", &day) == 0 && day.count == 0);
    Workout w;
    memset(&w, 0, sizeof w);
    ct_strlcpy(w.name, "Weights", sizeof w.name);
    w.minutes = 60;
    w.kcal = 212;
    CHECK(workouts_add(&day, &w) == 0);
    ct_strlcpy(w.name, "Soccer", sizeof w.name);
    w.kcal = 300;
    CHECK(workouts_add(&day, &w) == 0);
    CHECK(NEAR(workouts_total(&day), 512));
    CHECK(workouts_save(&day) == 0);

    WorkoutDay other;
    CHECK(workouts_load("2026-11-24", &other) == 0 && other.count == 0);
    other.items[0] = w;
    other.count = 1;
    CHECK(workouts_save(&other) == 0);

    CHECK(workouts_load("2026-11-23", &again) == 0 && again.count == 2);
    CHECK(strcmp(again.items[0].name, "Weights") == 0 && NEAR(again.items[1].kcal, 300));
    workouts_remove(&again, 0);
    CHECK(again.count == 1 && strcmp(again.items[0].name, "Soccer") == 0);
    workouts_remove(&again, 0);
    CHECK(workouts_save(&again) == 0);
    CHECK(workouts_load("2026-11-23", &again) == 0 && again.count == 0);
    CHECK(workouts_load("2026-11-24", &other) == 0 && other.count == 1);
}

int main(void)
{
    test_totals();
    test_dates();
    test_off();
    test_storage();
    test_cache_full();
    test_profile();
    test_activity();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all core tests passed\n");
    return 0;
}
