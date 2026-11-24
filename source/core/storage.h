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
/* Full path of a file in the data folder. */
void storage_path(char *out, size_t size, const char *name);

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

/* ---- favorites: foods logged in one tap with their usual grams --------- */
#define CT_MAX_FAVS 100

typedef struct {
    Food food;
    float grams;
} Favorite;

typedef struct {
    Favorite items[CT_MAX_FAVS];
    size_t count;
} FavList;

int favs_load(FavList *favs);
int favs_save(const FavList *favs);
/* Index of the food (matched on name + brand), or -1. */
int favs_find(const FavList *favs, const Food *food);
/* Adds or removes the food. Returns 1 if added, 0 if removed, -1 if full. */
int favs_toggle(FavList *favs, const Food *food, float grams);

/* ---- saved meals: a named group of entries logged together ------------- */
#define CT_MAX_MEAL_ITEMS 16
#define CT_MAX_SAVED_MEALS 40

typedef struct {
    char name[CT_NAME_LEN];
    Entry items[CT_MAX_MEAL_ITEMS];
    size_t count;
} SavedMeal;

typedef struct {
    SavedMeal items[CT_MAX_SAVED_MEALS];
    size_t count;
} MealList;

int meals_load(MealList *meals);
int meals_save(const MealList *meals);
int meals_add(MealList *meals, const SavedMeal *meal);
void meals_remove(MealList *meals, size_t index);

/* ---- brands the user follows, and a cache of their products ------------ */
#define CT_MAX_BRANDS 24

typedef struct {
    char names[CT_MAX_BRANDS][CT_BRAND_LEN];
    size_t count;
} BrandList;

int brands_load(BrandList *brands);
int brands_save(const BrandList *brands);
/* Returns 1 if added, 0 if already there (case-insensitive), -1 if full. */
int brands_add(BrandList *brands, const char *name);
void brands_remove(BrandList *brands, size_t index);
/* Returns the number of cached products, or -1 if nothing is cached. */
int brand_cache_load(const char *brand, Food *out, int max);
int brand_cache_save(const char *brand, const Food *foods, int count);

#endif
