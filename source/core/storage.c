#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../lib/cJSON.h"

static char g_base[256] = ".";

/* ---- helpers ---------------------------------------------------------- */

static int mkdir_p(const char *path)
{
    char tmp[256];
    ct_strlcpy(tmp, path, sizeof tmp);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            /* skip device prefixes such as "sdmc:" */
            if (p[-1] != ':' && mkdir(tmp, 0777) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

/* Write to a temp file first so a crash never leaves a half-written log. */
static int write_json(const char *path, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    if (!text)
        return -1;
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        free(text);
        return -1;
    }
    size_t len = strlen(text);
    int ok = fwrite(text, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    free(text);
    if (!ok)
        return -1;
    remove(path);
    return rename(tmp, path) == 0 ? 0 : -1;
}

static float num(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : 0.0f;
}

static const char *str(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static void food_to_json(cJSON *obj, const Food *food)
{
    cJSON_AddStringToObject(obj, "name", food->name);
    cJSON_AddStringToObject(obj, "brand", food->brand);
    cJSON_AddNumberToObject(obj, "kcal", food->kcal);
    cJSON_AddNumberToObject(obj, "protein", food->protein);
    cJSON_AddNumberToObject(obj, "carbs", food->carbs);
    cJSON_AddNumberToObject(obj, "fat", food->fat);
    cJSON_AddNumberToObject(obj, "serving_g", food->serving_g);
    cJSON *micro = NULL;
    for (int i = 0; i < MICRO_COUNT; i++) {
        if (food->micro[i] <= 0)
            continue;
        if (!micro)
            micro = cJSON_AddObjectToObject(obj, "micro");
        cJSON_AddNumberToObject(micro, micro_info((Micro)i)->json, food->micro[i]);
    }
}

static void food_from_json(const cJSON *obj, Food *food)
{
    ct_strlcpy(food->name, str(obj, "name"), sizeof food->name);
    ct_strlcpy(food->brand, str(obj, "brand"), sizeof food->brand);
    food->kcal = num(obj, "kcal");
    food->protein = num(obj, "protein");
    food->carbs = num(obj, "carbs");
    food->fat = num(obj, "fat");
    food->serving_g = num(obj, "serving_g");
    const cJSON *micro = cJSON_GetObjectItemCaseSensitive(obj, "micro");
    for (int i = 0; i < MICRO_COUNT; i++)
        food->micro[i] = micro ? num(micro, micro_info((Micro)i)->json) : 0;
}

static void entry_to_json(cJSON *obj, const Entry *e)
{
    food_to_json(obj, &e->food);
    cJSON_AddNumberToObject(obj, "grams", e->grams);
    cJSON_AddNumberToObject(obj, "meal", e->meal);
    if (e->quick)
        cJSON_AddTrueToObject(obj, "quick");
}

static void entry_from_json(const cJSON *obj, Entry *e)
{
    food_from_json(obj, &e->food);
    e->grams = num(obj, "grams");
    int meal = (int)num(obj, "meal");
    e->meal = (meal >= 0 && meal < MEAL_COUNT) ? (Meal)meal : MEAL_SNACKS;
    e->quick = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj, "quick"));
}

void storage_path(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s/%s", g_base, name);
}

static void path_for(char *out, size_t size, const char *name)
{
    storage_path(out, size, name);
}

/* ---- public API ------------------------------------------------------- */

int storage_init(const char *base_dir)
{
    ct_strlcpy(g_base, base_dir, sizeof g_base);
    char logs[300];
    path_for(logs, sizeof logs, "log");
    if (mkdir_p(g_base) != 0)
        return -1;
    return mkdir_p(logs);
}

int day_load(const char *date, DayLog *day)
{
    memset(day, 0, sizeof *day);
    ct_strlcpy(day->date, date, sizeof day->date);

    char path[300];
    snprintf(path, sizeof path, "%s/log/%s.json", g_base, date);
    char *text = read_file(path);
    if (!text)
        return 0;
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root)
        return -1;

    const cJSON *entries = cJSON_GetObjectItemCaseSensitive(root, "entries");
    const cJSON *item;
    cJSON_ArrayForEach(item, entries)
    {
        if (day->count >= CT_MAX_ENTRIES)
            break;
        entry_from_json(item, &day->entries[day->count++]);
    }
    cJSON_Delete(root);
    return 0;
}

int day_save(const DayLog *day)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "date", day->date);
    cJSON *entries = cJSON_AddArrayToObject(root, "entries");
    for (size_t i = 0; i < day->count; i++) {
        cJSON *obj = cJSON_CreateObject();
        entry_to_json(obj, &day->entries[i]);
        cJSON_AddItemToArray(entries, obj);
    }
    char path[300];
    snprintf(path, sizeof path, "%s/log/%s.json", g_base, day->date);
    int rc = write_json(path, root);
    cJSON_Delete(root);
    return rc;
}

int day_add(DayLog *day, const Entry *entry)
{
    if (day->count >= CT_MAX_ENTRIES)
        return -1;
    day->entries[day->count++] = *entry;
    return 0;
}

void day_remove(DayLog *day, size_t index)
{
    if (index >= day->count)
        return;
    memmove(&day->entries[index], &day->entries[index + 1],
            (day->count - index - 1) * sizeof(Entry));
    day->count--;
}

int foods_load(FoodCache *cache)
{
    memset(cache, 0, sizeof *cache);
    char path[300];
    path_for(path, sizeof path, "foods.json");
    char *text = read_file(path);
    if (!text)
        return 0;
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root)
        return -1;
    const cJSON *item;
    cJSON_ArrayForEach(item, root)
    {
        if (cache->count >= CT_MAX_FOODS)
            break;
        food_from_json(item, &cache->items[cache->count++]);
    }
    cJSON_Delete(root);
    return 0;
}

int foods_save(const FoodCache *cache)
{
    cJSON *root = cJSON_CreateArray();
    for (size_t i = 0; i < cache->count; i++) {
        cJSON *obj = cJSON_CreateObject();
        food_to_json(obj, &cache->items[i]);
        cJSON_AddItemToArray(root, obj);
    }
    char path[300];
    path_for(path, sizeof path, "foods.json");
    int rc = write_json(path, root);
    cJSON_Delete(root);
    return rc;
}

void foods_touch(FoodCache *cache, const Food *food)
{
    size_t i;
    for (i = 0; i < cache->count; i++) {
        if (strcmp(cache->items[i].name, food->name) == 0 &&
            strcmp(cache->items[i].brand, food->brand) == 0)
            break;
    }
    if (i == cache->count) {
        /* new food: drop the oldest if full */
        if (cache->count < CT_MAX_FOODS)
            cache->count++;
        i = cache->count - 1;
    }
    memmove(&cache->items[1], &cache->items[0], i * sizeof(Food));
    cache->items[0] = *food;
}

int targets_load(Targets *t)
{
    t->kcal = 2000;
    t->protein = 100;
    t->carbs = 250;
    t->fat = 70;
    char path[300];
    path_for(path, sizeof path, "targets.json");
    char *text = read_file(path);
    if (!text)
        return 0;
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root)
        return -1;
    if (num(root, "kcal") > 0) t->kcal = num(root, "kcal");
    if (num(root, "protein") > 0) t->protein = num(root, "protein");
    if (num(root, "carbs") > 0) t->carbs = num(root, "carbs");
    if (num(root, "fat") > 0) t->fat = num(root, "fat");
    cJSON_Delete(root);
    return 0;
}

int targets_save(const Targets *t)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "kcal", t->kcal);
    cJSON_AddNumberToObject(root, "protein", t->protein);
    cJSON_AddNumberToObject(root, "carbs", t->carbs);
    cJSON_AddNumberToObject(root, "fat", t->fat);
    char path[300];
    path_for(path, sizeof path, "targets.json");
    int rc = write_json(path, root);
    cJSON_Delete(root);
    return rc;
}

/* ---- helpers for the list files below ----------------------------------- */

cJSON *storage_load_json(const char *name)
{
    char path[300];
    path_for(path, sizeof path, name);
    char *text = read_file(path);
    if (!text)
        return NULL;
    cJSON *root = cJSON_Parse(text);
    free(text);
    return root;
}

int storage_save_json(const char *name, cJSON *root)
{
    char path[300];
    path_for(path, sizeof path, name);
    int rc = write_json(path, root);
    cJSON_Delete(root);
    return rc;
}

#define load_json storage_load_json
#define save_json storage_save_json

static int same_food(const Food *a, const Food *b)
{
    return strcmp(a->name, b->name) == 0 && strcmp(a->brand, b->brand) == 0;
}

/* ---- favorites ---------------------------------------------------------- */

int favs_load(FavList *favs)
{
    memset(favs, 0, sizeof *favs);
    cJSON *root = load_json("favorites.json");
    const cJSON *item;
    cJSON_ArrayForEach(item, root)
    {
        if (favs->count >= CT_MAX_FAVS)
            break;
        Favorite *f = &favs->items[favs->count++];
        food_from_json(item, &f->food);
        f->grams = num(item, "grams");
        if (f->grams <= 0)
            f->grams = 100;
    }
    cJSON_Delete(root);
    return 0;
}

int favs_save(const FavList *favs)
{
    cJSON *root = cJSON_CreateArray();
    for (size_t i = 0; i < favs->count; i++) {
        cJSON *obj = cJSON_CreateObject();
        food_to_json(obj, &favs->items[i].food);
        cJSON_AddNumberToObject(obj, "grams", favs->items[i].grams);
        cJSON_AddItemToArray(root, obj);
    }
    return save_json("favorites.json", root);
}

int favs_find(const FavList *favs, const Food *food)
{
    for (size_t i = 0; i < favs->count; i++)
        if (same_food(&favs->items[i].food, food))
            return (int)i;
    return -1;
}

int favs_toggle(FavList *favs, const Food *food, float grams)
{
    int i = favs_find(favs, food);
    if (i >= 0) {
        memmove(&favs->items[i], &favs->items[i + 1],
                (favs->count - (size_t)i - 1) * sizeof(Favorite));
        favs->count--;
        return 0;
    }
    if (favs->count >= CT_MAX_FAVS)
        return -1;
    favs->items[favs->count].food = *food;
    favs->items[favs->count].grams = grams > 0 ? grams : 100;
    favs->count++;
    return 1;
}

/* ---- saved meals -------------------------------------------------------- */

int meals_load(MealList *meals)
{
    memset(meals, 0, sizeof *meals);
    cJSON *root = load_json("meals.json");
    const cJSON *item;
    cJSON_ArrayForEach(item, root)
    {
        if (meals->count >= CT_MAX_SAVED_MEALS)
            break;
        SavedMeal *m = &meals->items[meals->count++];
        ct_strlcpy(m->name, str(item, "name"), sizeof m->name);
        const cJSON *e;
        cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(item, "items"))
        {
            if (m->count >= CT_MAX_MEAL_ITEMS)
                break;
            entry_from_json(e, &m->items[m->count++]);
        }
    }
    cJSON_Delete(root);
    return 0;
}

int meals_save(const MealList *meals)
{
    cJSON *root = cJSON_CreateArray();
    for (size_t i = 0; i < meals->count; i++) {
        const SavedMeal *m = &meals->items[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name", m->name);
        cJSON *items = cJSON_AddArrayToObject(obj, "items");
        for (size_t j = 0; j < m->count; j++) {
            cJSON *e = cJSON_CreateObject();
            entry_to_json(e, &m->items[j]);
            cJSON_AddItemToArray(items, e);
        }
        cJSON_AddItemToArray(root, obj);
    }
    return save_json("meals.json", root);
}

int meals_add(MealList *meals, const SavedMeal *meal)
{
    if (meals->count >= CT_MAX_SAVED_MEALS)
        return -1;
    meals->items[meals->count++] = *meal;
    return 0;
}

void meals_remove(MealList *meals, size_t index)
{
    if (index >= meals->count)
        return;
    memmove(&meals->items[index], &meals->items[index + 1],
            (meals->count - index - 1) * sizeof(SavedMeal));
    meals->count--;
}

/* ---- brands ------------------------------------------------------------- */

int brands_load(BrandList *brands)
{
    memset(brands, 0, sizeof *brands);
    cJSON *root = load_json("brands.json");
    const cJSON *item;
    cJSON_ArrayForEach(item, root)
    {
        if (brands->count >= CT_MAX_BRANDS)
            break;
        if (cJSON_IsString(item) && item->valuestring[0])
            ct_strlcpy(brands->names[brands->count++], item->valuestring, CT_BRAND_LEN);
    }
    cJSON_Delete(root);
    return 0;
}

int brands_save(const BrandList *brands)
{
    cJSON *root = cJSON_CreateArray();
    for (size_t i = 0; i < brands->count; i++)
        cJSON_AddItemToArray(root, cJSON_CreateString(brands->names[i]));
    return save_json("brands.json", root);
}

static int ascii_casecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb || !ca)
            return ca - cb;
    }
}

int brands_add(BrandList *brands, const char *name)
{
    for (size_t i = 0; i < brands->count; i++)
        if (ascii_casecmp(brands->names[i], name) == 0)
            return 0; /* already there */
    if (brands->count >= CT_MAX_BRANDS)
        return -1;
    ct_strlcpy(brands->names[brands->count++], name, CT_BRAND_LEN);
    return 1;
}

void brands_remove(BrandList *brands, size_t index)
{
    if (index >= brands->count)
        return;
    memmove(brands->names[index], brands->names[index + 1],
            (brands->count - index - 1) * CT_BRAND_LEN);
    brands->count--;
}

/* "Quest Nutrition" -> "brand_quest-nutrition.json" */
static void brand_file(const char *brand, char *out, size_t size)
{
    char slug[CT_BRAND_LEN];
    size_t n = 0;
    for (const char *p = brand; *p && n + 1 < sizeof slug; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            slug[n++] = c;
        else if (n > 0 && slug[n - 1] != '-')
            slug[n++] = '-';
    }
    slug[n] = '\0';
    snprintf(out, size, "brand_%s.json", slug);
}

int brand_cache_load(const char *brand, Food *out, int max)
{
    char name[80];
    brand_file(brand, name, sizeof name);
    cJSON *root = load_json(name);
    if (!root)
        return -1;
    int n = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, root)
    {
        if (n >= max)
            break;
        food_from_json(item, &out[n++]);
    }
    cJSON_Delete(root);
    return n;
}

int brand_cache_save(const char *brand, const Food *foods, int count)
{
    char name[80];
    brand_file(brand, name, sizeof name);
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *obj = cJSON_CreateObject();
        food_to_json(obj, &foods[i]);
        cJSON_AddItemToArray(root, obj);
    }
    return save_json(name, root);
}
