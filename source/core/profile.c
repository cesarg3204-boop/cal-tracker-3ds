#include "profile.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/cJSON.h"
#include "food.h"
#include "storage.h"

static const char *ACTIVITY_NAMES[ACTIVITY_COUNT] = {"Sedentary", "Lightly active", "Moderately active",
                                                     "Active", "Very active"};
static const float ACTIVITY_FACTORS[ACTIVITY_COUNT] = {1.2f, 1.375f, 1.5f, 1.725f, 1.9f};

const char *activity_name(Activity a)
{
    return (a >= 0 && a < ACTIVITY_COUNT) ? ACTIVITY_NAMES[a] : "?";
}

float activity_factor(Activity a)
{
    return (a >= 0 && a < ACTIVITY_COUNT) ? ACTIVITY_FACTORS[a] : 1.2f;
}

float bmi(float kg, float height_cm)
{
    if (kg <= 0 || height_cm <= 0)
        return 0;
    float m = height_cm / 100.0f;
    return kg / (m * m);
}

float bmr(const UserProfile *p, float kg)
{
    if (kg <= 0 || p->height_cm <= 0 || p->age <= 0)
        return 0;
    float base = 10.0f * kg + 6.25f * p->height_cm - 5.0f * (float)p->age;
    return base + (p->sex == SEX_FEMALE ? -161.0f : 5.0f);
}

/* ---- files -------------------------------------------------------------- */

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = len >= 0 ? malloc((size_t)len + 1) : NULL;
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

static int write_all(const char *path, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text)
        return -1;
    char tmp[320];
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

int profile_load(UserProfile *p)
{
    memset(p, 0, sizeof *p);
    p->activity = ACTIVITY_MODERATE;
    p->water_goal = 8;
    char path[300];
    storage_path(path, sizeof path, "profile.json");
    char *text = read_all(path);
    if (!text)
        return 0;
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root)
        return -1;
    p->age = (int)num(root, "age");
    p->sex = num(root, "sex") >= 1 ? SEX_FEMALE : SEX_MALE;
    p->height_cm = num(root, "height_cm");
    p->body_fat = num(root, "body_fat");
    int a = (int)num(root, "activity");
    p->activity = (a >= 0 && a < ACTIVITY_COUNT) ? (Activity)a : ACTIVITY_MODERATE;
    int w = (int)num(root, "water_goal");
    p->water_goal = w > 0 && w <= 30 ? w : 8;
    p->sounds_off = num(root, "sounds_off") >= 1;
    cJSON_Delete(root);
    return 0;
}

int profile_save(const UserProfile *p)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "age", p->age);
    cJSON_AddNumberToObject(root, "sex", p->sex);
    cJSON_AddNumberToObject(root, "height_cm", p->height_cm);
    cJSON_AddNumberToObject(root, "body_fat", p->body_fat);
    cJSON_AddNumberToObject(root, "activity", p->activity);
    cJSON_AddNumberToObject(root, "water_goal", p->water_goal);
    cJSON_AddNumberToObject(root, "sounds_off", p->sounds_off);
    char path[300];
    storage_path(path, sizeof path, "profile.json");
    return write_all(path, root);
}

int series_load(Series *s, const char *file)
{
    memset(s, 0, sizeof *s);
    char path[300];
    storage_path(path, sizeof path, file);
    char *text = read_all(path);
    if (!text)
        return 0;
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root)
        return -1;
    const cJSON *item;
    cJSON_ArrayForEach(item, root)
    {
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(item, "date");
        float v = num(item, "value");
        if (cJSON_IsString(d) && v > 0)
            series_set(s, d->valuestring, v);
    }
    cJSON_Delete(root);
    return 0;
}

int series_save(const Series *s, const char *file)
{
    cJSON *root = cJSON_CreateArray();
    for (size_t i = 0; i < s->count; i++) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "date", s->items[i].date);
        cJSON_AddNumberToObject(obj, "value", s->items[i].value);
        cJSON_AddItemToArray(root, obj);
    }
    char path[300];
    storage_path(path, sizeof path, file);
    return write_all(path, root);
}

int series_set(Series *s, const char *date, float value)
{
    size_t i = 0;
    while (i < s->count && strcmp(s->items[i].date, date) < 0)
        i++;
    bool found = i < s->count && strcmp(s->items[i].date, date) == 0;
    if (value <= 0) {
        if (found) {
            memmove(&s->items[i], &s->items[i + 1], (s->count - i - 1) * sizeof(SeriesPoint));
            s->count--;
        }
        return 0;
    }
    if (found) {
        s->items[i].value = value;
        return 0;
    }
    if (s->count >= CT_MAX_SERIES) {
        /* full: drop the oldest */
        if (i == 0)
            return -1;
        memmove(&s->items[0], &s->items[1], (i - 1) * sizeof(SeriesPoint));
        i--;
    } else {
        memmove(&s->items[i + 1], &s->items[i], (s->count - i) * sizeof(SeriesPoint));
        s->count++;
    }
    ct_strlcpy(s->items[i].date, date, CT_DATE_LEN);
    s->items[i].value = value;
    return 0;
}

float series_get(const Series *s, const char *date)
{
    for (size_t i = 0; i < s->count; i++)
        if (strcmp(s->items[i].date, date) == 0)
            return s->items[i].value;
    return 0;
}

float series_latest(const Series *s, const char *date)
{
    for (size_t i = s->count; i > 0; i--) {
        const SeriesPoint *p = &s->items[i - 1];
        if (!date || strcmp(p->date, date) <= 0)
            return p->value;
    }
    return 0;
}

int goal_day(float kcal, float protein, float kcal_target, float protein_target)
{
    if (kcal_target <= 0 || kcal <= 0)
        return 0;
    float r = kcal / kcal_target;
    if (r < 0.9f || r > 1.1f)
        return 0;
    return protein_target <= 0 || protein >= protein_target * 0.9f;
}

int streak_before(const char *today, int (*hit)(const char *date, void *ctx), void *ctx, int max_days)
{
    char date[CT_DATE_LEN];
    int n = 0;
    for (int back = 1; back <= max_days; back++) {
        if (date_add_days(today, -back, date) != 0 || !hit(date, ctx))
            break;
        n++;
    }
    return n;
}
