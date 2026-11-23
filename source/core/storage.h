#ifndef CT_STORAGE_H
#define CT_STORAGE_H

#include "date.h"
#include "food.h"

#define CT_MAX_ENTRIES 128
#define CT_MAX_FOODS 400

typedef struct {
    char date[CT_DATE_LEN];
    Entry entries[CT_MAX_ENTRIES];
    size_t count;
} DayLog;

typedef struct {
    Food items[CT_MAX_FOODS]; /* most recently used first */
    size_t count;
} FoodCache;

typedef struct {
    float kcal;
    float protein;
    float carbs;
    float fat;
} Targets;

/* Set the data folder (e.g. "sdmc:/3ds/caltracker") and create it. */
int storage_init(const char *base_dir);

/* A missing file is not an error: you get an empty day / cache / defaults. */
int day_load(const char *date, DayLog *day);
int day_save(const DayLog *day);
int day_add(DayLog *day, const Entry *entry);
void day_remove(DayLog *day, size_t index);

int foods_load(FoodCache *cache);
int foods_save(const FoodCache *cache);
/* Move (or insert) a food to the front, matching on name + brand. */
void foods_touch(FoodCache *cache, const Food *food);

int targets_load(Targets *targets);
int targets_save(const Targets *targets);

#endif
