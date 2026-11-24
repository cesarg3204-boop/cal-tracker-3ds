#ifndef CT_ACTIVITY_H
#define CT_ACTIVITY_H

/* Energy used on top of resting energy (BMR): steps from the 3DS pedometer
 * and workouts typed in by hand. */

#include <stddef.h>

#include "date.h"

/* ---- steps -------------------------------------------------------------- */

/* Calories for walking `steps`, beyond resting energy. Stride comes from
 * height (0.414 x height, or 70 cm if unknown); walking uses about
 * 0.5 kcal per kg per km. Returns 0 without a weight. */
float steps_kcal(unsigned steps, float kg, float height_cm);

/* The pedometer's clock: milliseconds since 2000-01-01 00:00 (console
 * local time), at the midnight that starts `date`. Returns 0 or -1. */
int date_pedometer_ms(const char *date, long long *ms);

/* ---- workouts ----------------------------------------------------------- */

typedef struct {
    const char *name;
    float met; /* 0: the user types the calories */
} WorkoutType;

extern const WorkoutType WORKOUT_TYPES[];
extern const int WORKOUT_TYPE_COUNT;

/* Calories for `minutes` of an activity of `met`, beyond resting energy
 * ((MET - 1) x kg x hours), or 0 without a weight. */
float workout_kcal(float met, float kg, float minutes);

#define CT_MAX_WORKOUTS 12
#define CT_WORKOUT_NAME 32

typedef struct {
    char name[CT_WORKOUT_NAME];
    float minutes; /* 0 if not given */
    float kcal;
} Workout;

typedef struct {
    char date[CT_DATE_LEN];
    Workout items[CT_MAX_WORKOUTS];
    size_t count;
} WorkoutDay;

/* workouts.json holds every day: {"2026-11-23": [{name, minutes, kcal}]}.
 * Uses the folder set by storage_init(); a missing day is empty. */
int workouts_load(const char *date, WorkoutDay *day);
int workouts_save(const WorkoutDay *day);
int workouts_add(WorkoutDay *day, const Workout *w); /* -1 if full */
void workouts_remove(WorkoutDay *day, size_t index);
float workouts_total(const WorkoutDay *day);

#endif
