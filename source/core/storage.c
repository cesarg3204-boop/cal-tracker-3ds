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
}

static void path_for(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s/%s", g_base, name);
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
        Entry *e = &day->entries[day->count++];
        food_from_json(item, &e->food);
        e->grams = num(item, "grams");
        int meal = (int)num(item, "meal");
        e->meal = (meal >= 0 && meal < MEAL_COUNT) ? (Meal)meal : MEAL_SNACKS;
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
        food_to_json(obj, &day->entries[i].food);
        cJSON_AddNumberToObject(obj, "grams", day->entries[i].grams);
        cJSON_AddNumberToObject(obj, "meal", day->entries[i].meal);
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
