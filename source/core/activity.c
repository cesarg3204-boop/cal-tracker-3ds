#include "activity.h"

#include <string.h>

#include "../lib/cJSON.h"
#include "food.h"
#include "storage.h"

#define WORKOUTS_FILE "workouts.json"

/* MET values from the Compendium of Physical Activities. Walking is left
 * out: the pedometer already counts it. */
const WorkoutType WORKOUT_TYPES[] = {
    {"Weights", 5.0f},
    {"Weights (light)", 3.5f},
    {"Running", 9.8f},
    {"Cycling", 7.5f},
    {"Swimming", 7.0f},
    {"Soccer / sports", 7.0f},
    {"HIIT / circuits", 8.0f},
    {"Basketball", 6.5f},
    {"Dance", 5.0f},
    {"Yoga / stretching", 2.5f},
    {"Other (type kcal)", 0.0f},
};
const int WORKOUT_TYPE_COUNT = (int)(sizeof WORKOUT_TYPES / sizeof WORKOUT_TYPES[0]);

float steps_kcal(unsigned steps, float kg, float height_cm)
{
    if (kg <= 0)
        return 0;
    float stride_m = height_cm > 0 ? height_cm * 0.00414f : 0.70f;
    float km = (float)steps * stride_m / 1000.0f;
    return 0.5f * kg * km;
}

int date_pedometer_ms(const char *date, long long *ms)
{
    long days;
    if (date_to_days(date, &days) != 0)
        return -1;
    long since2000 = days - 10957; /* 2000-01-01 is day 10957 after 1970-01-01 */
    *ms = (long long)since2000 * 86400LL * 1000LL;
    return 0;
}

float workout_kcal(float met, float kg, float minutes)
{
    if (kg <= 0 || minutes <= 0 || met <= 1.0f)
        return 0;
    return (met - 1.0f) * kg * minutes / 60.0f;
}

/* ---- storage ------------------------------------------------------------ */

int workouts_load(const char *date, WorkoutDay *day)
{
    memset(day, 0, sizeof *day);
    ct_strlcpy(day->date, date, sizeof day->date);
    cJSON *root = storage_load_json(WORKOUTS_FILE);
    if (!root)
        return 0;
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, date);
    const cJSON *item;
    cJSON_ArrayForEach(item, list)
    {
        if (day->count >= CT_MAX_WORKOUTS)
            break;
        Workout *w = &day->items[day->count];
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        const cJSON *minutes = cJSON_GetObjectItemCaseSensitive(item, "minutes");
        const cJSON *kcal = cJSON_GetObjectItemCaseSensitive(item, "kcal");
        if (!cJSON_IsString(name) || !cJSON_IsNumber(kcal))
            continue;
        ct_strlcpy(w->name, name->valuestring, sizeof w->name);
        w->minutes = cJSON_IsNumber(minutes) ? (float)minutes->valuedouble : 0;
        w->kcal = (float)kcal->valuedouble;
        day->count++;
    }
    cJSON_Delete(root);
    return 0;
}

int workouts_save(const WorkoutDay *day)
{
    cJSON *root = storage_load_json(WORKOUTS_FILE);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    cJSON_DeleteItemFromObjectCaseSensitive(root, day->date);
    if (day->count > 0) {
        cJSON *list = cJSON_AddArrayToObject(root, day->date);
        for (size_t i = 0; i < day->count; i++) {
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddStringToObject(obj, "name", day->items[i].name);
            cJSON_AddNumberToObject(obj, "minutes", day->items[i].minutes);
            cJSON_AddNumberToObject(obj, "kcal", day->items[i].kcal);
            cJSON_AddItemToArray(list, obj);
        }
    }
    return storage_save_json(WORKOUTS_FILE, root);
}

int workouts_add(WorkoutDay *day, const Workout *w)
{
    if (day->count >= CT_MAX_WORKOUTS)
        return -1;
    day->items[day->count++] = *w;
    return 0;
}

void workouts_remove(WorkoutDay *day, size_t index)
{
    if (index >= day->count)
        return;
    memmove(&day->items[index], &day->items[index + 1], (day->count - index - 1) * sizeof(Workout));
    day->count--;
}

float workouts_total(const WorkoutDay *day)
{
    float t = 0;
    for (size_t i = 0; i < day->count; i++)
        t += day->items[i].kcal;
    return t;
}
