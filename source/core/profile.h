#ifndef CT_PROFILE_H
#define CT_PROFILE_H

/* Personal profile, weight history and energy estimates. */

#include <stddef.h>

#include "date.h"

typedef enum { SEX_MALE = 0, SEX_FEMALE = 1 } Sex;

typedef enum {
    ACTIVITY_SEDENTARY = 0,
    ACTIVITY_LIGHT,
    ACTIVITY_MODERATE,
    ACTIVITY_ACTIVE,
    ACTIVITY_VERY_ACTIVE,
    ACTIVITY_COUNT
} Activity;

typedef struct {
    int age;          /* years, 0 if unknown */
    Sex sex;
    float height_cm;  /* 0 if unknown */
    float body_fat;   /* percent, 0 if unknown */
    Activity activity;
} UserProfile;

/* One number per date: body weight (weights.json) or exercise kcal
 * (exercise.json). */
#define CT_MAX_SERIES 400
#define WEIGHTS_FILE "weights.json"
#define EXERCISE_FILE "exercise.json"

typedef struct {
    char date[CT_DATE_LEN];
    float value;
} SeriesPoint;

typedef struct {
    SeriesPoint items[CT_MAX_SERIES]; /* sorted by date, oldest first */
    size_t count;
} Series;

const char *activity_name(Activity a);
/* Multiplier on BMR for everyday (non-exercise) activity. */
float activity_factor(Activity a);

/* Body mass index, or 0 if weight or height is missing. */
float bmi(float kg, float height_cm);
/* Resting energy (Mifflin-St Jeor), kcal per day, or 0 if data is missing. */
float bmr(const UserProfile *p, float kg);

/* Uses the folder set by storage_init(). Missing files give defaults. */
int profile_load(UserProfile *p);
int profile_save(const UserProfile *p);

int series_load(Series *s, const char *file);
int series_save(const Series *s, const char *file);
/* Adds or replaces the value for that date, keeping the series sorted.
 * A value of 0 removes the date. */
int series_set(Series *s, const char *date, float value);
/* Value for exactly that date, or 0. */
float series_get(const Series *s, const char *date);
/* Latest value on or before `date` (NULL = any), or 0 if none. */
float series_latest(const Series *s, const char *date);

#endif
