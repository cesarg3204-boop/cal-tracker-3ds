/* Cal Pop: a colorful calorie and macro tracker for the 3DS.
 *
 * Top screen:    the current tab's content (today's log, search results,
 *                brand tiles and products, favorites, saved meals, profile).
 * Bottom screen: details of the selected item, touch buttons, and a tab bar.
 * Every touch button also has a button shortcut, shown on its label.
 *
 * Colors follow one gradient: green -> turquoise -> blue.
 */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/activity.h"
#include "core/barcode.h"
#include "core/date.h"
#include "core/food.h"
#include "core/off.h"
#include "core/profile.h"
#include "core/storage.h"
#include "net.h"
#include "pedometer.h"
#include "scanner.h"
#include "sound.h"

#define DATA_DIR "sdmc:/3ds/caltracker" /* shared with the plain Cal Tracker */
#define TOP_W 400.0f
#define BOT_W 320.0f
#define SCREEN_H 240.0f
#define ROW_H 15.0f
#define TEXT_SCALE 0.5f
#define MAX_BRAND_FOODS 60

/* ---- palette ------------------------------------------------------------ */
static u32 C_BG, C_PANEL, C_PANEL2, C_TEXT, C_INK, C_MUTED, C_BAR_BG, C_WARN, C_MINT;
static u32 C_GREEN, C_TURQ, C_BLUE;
static u32 C_PROTEIN, C_CARBS, C_FAT;
/* Button color meaning "take it from the gradient at the button's spot".
 * Real colors always have alpha 0xff, so they are never 0. */
#define C_GRAD 0u

static void init_colors(void)
{
    C_BG = C2D_Color32(0x0b, 0x1d, 0x2a, 0xff);     /* deep navy teal */
    C_PANEL = C2D_Color32(0x12, 0x30, 0x3f, 0xff);
    C_PANEL2 = C2D_Color32(0x1b, 0x43, 0x57, 0xff); /* selection */
    C_TEXT = C2D_Color32(0xff, 0xff, 0xff, 0xff);
    C_INK = C2D_Color32(0x05, 0x1e, 0x22, 0xff); /* dark text on bright fills */
    C_MUTED = C2D_Color32(0x9e, 0xd9, 0xd2, 0xff);
    C_BAR_BG = C2D_Color32(0x1e, 0x4a, 0x5c, 0xff);
    C_WARN = C2D_Color32(0xfb, 0x71, 0x85, 0xff); /* over a target */
    C_MINT = C2D_Color32(0xbb, 0xf7, 0xd0, 0xff); /* favorites, status line */
    C_GREEN = C2D_Color32(0x22, 0xc5, 0x5e, 0xff);
    C_TURQ = C2D_Color32(0x2d, 0xd4, 0xbf, 0xff);
    C_BLUE = C2D_Color32(0x3b, 0x82, 0xf6, 0xff);
    C_PROTEIN = C_GREEN;
    C_CARBS = C_TURQ;
    C_FAT = C_BLUE;
}

/* The app gradient at t (0 = green, 0.5 = turquoise, 1 = blue). */
static u32 grad(float t)
{
    static const u8 stops[3][3] = {{0x22, 0xc5, 0x5e}, {0x2d, 0xd4, 0xbf}, {0x3b, 0x82, 0xf6}};
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int s = t < 0.5f ? 0 : 1;
    float f = (t - s * 0.5f) * 2;
    u8 c[3];
    for (int i = 0; i < 3; i++)
        c[i] = (u8)(stops[s][i] + (stops[s + 1][i] - stops[s][i]) * f + 0.5f);
    return C2D_Color32(c[0], c[1], c[2], 0xff);
}

/* ---- tabs and actions --------------------------------------------------- */
typedef enum { TAB_TODAY, TAB_SEARCH, TAB_BRANDS, TAB_FAVS, TAB_MEALS, TAB_PROFILE, TAB_COUNT } Tab;
static const char *TAB_NAMES[TAB_COUNT] = {"Today", "Search", "Brands", "Favs", "Meals", "Profile"};

typedef enum { PAGE_PROFILE, PAGE_ACTIVITY, PAGE_REPORT, PAGE_CHARTS, PAGE_COUNT } Page;
static const char *PAGE_NAMES[PAGE_COUNT] = {"Profile", "Activity", "Report", "Charts"};

typedef enum {
    PR_AGE,
    PR_SEX,
    PR_WEIGHT,
    PR_HEIGHT,
    PR_BMI,
    PR_BODY_FAT,
    PR_ACTIVITY,
    PR_WATER,
    PR_SOUNDS,
    PR_KCAL,
    PR_PROTEIN,
    PR_CARBS,
    PR_FAT,
    PR_COUNT
} ProfileRow;

typedef enum {
    ACT_NONE,
    ACT_GO_SEARCH,
    ACT_GO_MEALS,
    ACT_LOG,
    ACT_LOG_GRAMS,
    ACT_STAR,
    ACT_SEARCH,
    ACT_QUICK,
    ACT_PREV_DAY,
    ACT_NEXT_DAY,
    ACT_TODAY,
    ACT_DELETE,
    ACT_EDIT_GRAMS,
    ACT_ADD_BRAND,
    ACT_REMOVE_BRAND,
    ACT_OPEN_BRAND,
    ACT_BACK,
    ACT_REFRESH,
    ACT_MEAL_PREV,
    ACT_MEAL_NEXT,
    ACT_LOG_SCALED,
    ACT_NEW_MEAL,
    ACT_SAVE_DRAFT,
    ACT_CANCEL_DRAFT,
    ACT_PAGE_PROFILE,
    ACT_PAGE_ACTIVITY,
    ACT_PAGE_REPORT,
    ACT_PAGE_CHARTS,
    ACT_EDIT_ROW,
    ACT_LOG_WEIGHT,
    ACT_ADD_WORKOUT,
    ACT_REMOVE_WORKOUT,
    ACT_PICK_CHOOSE,
    ACT_PICK_CANCEL,
    ACT_PREV_WEEK,
    ACT_NEXT_WEEK,
    ACT_SCAN,
    ACT_SCAN_CANCEL,
    ACT_WATER_ADD,
    ACT_WATER_REMOVE,
    ACT_MICROS,
    ACT_MICROS_BACK,
    ACT_UNIT_CHOOSE,
    ACT_UNIT_CANCEL,
} Action;

typedef struct {
    float x, y, w, h;
    const char *label;
    u32 color;
    Action action;
} Button;

#define MAX_BUTTONS 12
static Button g_buttons[MAX_BUTTONS];
static int g_button_count;

/* ---- app state ---------------------------------------------------------- */
static DayLog g_day;
static FoodCache g_cache;
static Targets g_targets;
static FavList g_favs;
static MealList g_meals;
static BrandList g_brands;
static UserProfile g_profile;
static Series g_weights, g_steps, g_water;

/* Activity for the viewed day: steps from the 3DS and typed-in workouts. */
static uint16_t g_step_hours[24];
static unsigned g_step_total = 0;
static bool g_steps_live = false; /* read from the pedometer, not the saved copy */
static WorkoutDay g_workouts;
static int g_workout_sel = 0;
static bool g_picking = false; /* choosing a workout type */
static int g_pick_sel = 0;
static char g_today[CT_DATE_LEN];

static Tab g_tab = TAB_TODAY;
static Page g_page = PAGE_PROFILE;
static int g_sel[TAB_COUNT], g_scroll[TAB_COUNT];
static Meal g_slot = MEAL_BREAKFAST; /* where new food is logged */

static Food g_results[OFF_PAGE_SIZE];
static int g_result_count = 0;
static bool g_have_results = false; /* false: Search shows recent foods */
static char g_query[64] = "";

static int g_brand_open = -1; /* index into g_brands while browsing a brand */
static Food g_brand_foods[MAX_BRAND_FOODS];
static int g_brand_count = 0;
static int g_brand_sel = 0, g_brand_scroll = 0;

/* Meal builder: while on, "A" on Search, Favs and Brands adds to the draft. */
static bool g_building = false;
static SavedMeal g_draft;

/* The 7 days ending on the viewed day, for the charts. */
#define WEEK 7
static Totals g_week[WEEK];
static char g_week_dates[WEEK][CT_DATE_LEN];
static bool g_week_dirty = true;

typedef enum { PEND_NONE, PEND_SEARCH, PEND_BRAND, PEND_PRODUCT } Pending;
static Pending g_pending = PEND_NONE;
static char g_status[96] = "";
static bool g_net_ok = false;

static size_t g_order[CT_MAX_ENTRIES]; /* today's entries grouped by meal */
static C2D_TextBuf g_textbuf;

/* round 3: scanner, micronutrients page, streak, mascot */
static bool g_scanning = false;
static char g_barcode[BARCODE_LEN] = "";
static bool g_show_micros = false; /* Today: the "all micronutrients" page */
static int g_streak_prev = 0;      /* goal days in a row before today */
static bool g_today_hit = false;   /* today is a goal day so far */
static unsigned g_anim = 0;        /* frame counter for animations */
static int g_cheer = 0;            /* frames left of the mascot cheering */

/* "How much?" picker: servings, grams, ounces... */
typedef enum { UNIT_FOR_LOG, UNIT_FOR_EDIT } UnitMode;
static bool g_unit_picking = false;
static UnitMode g_unit_mode = UNIT_FOR_LOG;
static Food g_unit_food;
static Unit g_unit_opts[UNIT_COUNT];
static int g_unit_n = 0, g_unit_sel = 0;

/* ---- drawing helpers ---------------------------------------------------- */
static void text(float x, float y, float scale, u32 color, u32 flags, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    C2D_Text t;
    C2D_TextParse(&t, g_textbuf, buf);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | flags, x, y, 0.5f, scale, scale, color);
}

/* Like text(), but shrinks the text (down to 70%) so it fits in max_w. */
static void text_fit(float x, float y, float scale, float max_w, u32 color, u32 flags, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    C2D_Text t;
    C2D_TextParse(&t, g_textbuf, buf);
    C2D_TextOptimize(&t);
    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    if (w > max_w && w > 0) {
        float s2 = scale * max_w / w;
        scale = s2 < scale * 0.7f ? scale * 0.7f : s2;
    }
    C2D_DrawText(&t, C2D_WithColor | flags, x, y, 0.5f, scale, scale, color);
}

/* Rounded rectangle. Use opaque colors: the pieces overlap. */
static void rrect(float x, float y, float w, float h, float r, u32 color)
{
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    C2D_DrawRectSolid(x + r, y, 0.4f, w - 2 * r, h, color);
    C2D_DrawRectSolid(x, y + r, 0.4f, w, h - 2 * r, color);
    C2D_DrawCircleSolid(x + r, y + r, 0.4f, r, color);
    C2D_DrawCircleSolid(x + w - r, y + r, 0.4f, r, color);
    C2D_DrawCircleSolid(x + r, y + h - r, 0.4f, r, color);
    C2D_DrawCircleSolid(x + w - r, y + h - r, 0.4f, r, color);
}

/* Left-to-right gradient from grad(t0) to grad(t1), in slices so the
 * turquoise middle shows on wide shapes. */
static void grad_rect(float x, float y, float w, float h, float t0, float t1)
{
    if (w <= 0)
        return;
    const int n = 4;
    float sw = w / n;
    for (int i = 0; i < n; i++) {
        u32 a = grad(t0 + (t1 - t0) * i / n), b = grad(t0 + (t1 - t0) * (i + 1) / n);
        float ww = i == n - 1 ? sw : sw + 0.5f; /* overlap to hide seams */
        C2D_DrawRectangle(x + i * sw, y, 0.4f, ww, h, a, b, a, b);
    }
}

static void rrect_grad(float x, float y, float w, float h, float r, float t0, float t1)
{
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    u32 cl = grad(t0), cr = grad(t1);
    grad_rect(x + r, y, w - 2 * r, h, t0, t1);
    C2D_DrawRectSolid(x, y + r, 0.4f, r, h - 2 * r, cl);
    C2D_DrawRectSolid(x + w - r, y + r, 0.4f, r, h - 2 * r, cr);
    C2D_DrawCircleSolid(x + r, y + r, 0.4f, r, cl);
    C2D_DrawCircleSolid(x + r, y + h - r, 0.4f, r, cl);
    C2D_DrawCircleSolid(x + w - r, y + r, 0.4f, r, cr);
    C2D_DrawCircleSolid(x + w - r, y + h - r, 0.4f, r, cr);
}

/* Progress bar. color C_GRAD fills with the gradient. */
static void bar(float x, float y, float w, float h, float value, float target, u32 color)
{
    rrect(x, y, w, h, h / 2, C_BAR_BG);
    if (target <= 0 || value <= 0)
        return;
    float frac = value / target;
    bool over = frac > 1.0f;
    if (over)
        frac = 1.0f;
    float fw = w * frac;
    if (fw < h)
        fw = h; /* keep the rounded end visible */
    if (over)
        rrect(x, y, fw, h, h / 2, C_WARN);
    else if (color == C_GRAD)
        rrect_grad(x, y, fw, h, h / 2, 0, frac);
    else
        rrect(x, y, fw, h, h / 2, color);
}

static void status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_status, sizeof g_status, fmt, ap);
    va_end(ap);
}

static void add_button(float x, float y, float w, float h, const char *label, u32 color, Action a)
{
    if (g_button_count >= MAX_BUTTONS)
        return;
    Button b = {x, y, w, h, label, color, a};
    g_buttons[g_button_count++] = b;
}

/* ---- keyboard prompts --------------------------------------------------- */
static bool prompt_text(const char *hint, const char *initial, char *out, size_t size)
{
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, (int)size - 1);
    swkbdSetHintText(&kb, hint);
    if (initial)
        swkbdSetInitialText(&kb, initial);
    swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    return swkbdInputText(&kb, out, size) == SWKBD_BUTTON_RIGHT;
}

static bool prompt_number(const char *hint, float initial, float *out)
{
    char buf[16];
    char init[16];
    snprintf(init, sizeof init, "%g", initial);
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NUMPAD, 2, 8);
    swkbdSetHintText(&kb, hint);
    swkbdSetInitialText(&kb, init);
    swkbdSetNumpadKeys(&kb, '.', 0);
    swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    if (swkbdInputText(&kb, buf, sizeof buf) != SWKBD_BUTTON_RIGHT)
        return false;
    char *end;
    float v = strtof(buf, &end);
    if (end == buf || v < 0)
        return false;
    *out = v;
    return true;
}

/* ---- data helpers ------------------------------------------------------- */
static void rebuild_order(void)
{
    size_t n = 0;
    for (int m = 0; m < MEAL_COUNT; m++)
        for (size_t i = 0; i < g_day.count; i++)
            if ((int)g_day.entries[i].meal == m)
                g_order[n++] = i;
}

static void reset_list(Tab t)
{
    g_sel[t] = 0;
    g_scroll[t] = 0;
}

/* Re-reads steps and workouts for the viewed day. Steps that the
 * pedometer reports are saved, so days it has forgotten still show. */
static void refresh_activity(void)
{
    unsigned total = 0;
    g_steps_live = pedometer_day(g_day.date, g_step_hours, &total) == 0 && total > 0;
    if (g_steps_live) {
        if ((unsigned)series_get(&g_steps, g_day.date) != total) {
            series_set(&g_steps, g_day.date, (float)total);
            series_save(&g_steps, STEPS_FILE);
        }
    } else {
        memset(g_step_hours, 0, sizeof g_step_hours);
        total = (unsigned)series_get(&g_steps, g_day.date);
    }
    g_step_total = total;
    if (strcmp(g_workouts.date, g_day.date) != 0) {
        workouts_load(g_day.date, &g_workouts);
        g_workout_sel = 0;
    }
}

/* Goal days: calories near the target and enough protein. */
static int day_hit(const char *date, void *ctx)
{
    (void)ctx;
    static DayLog tmp;
    if (day_load(date, &tmp) != 0)
        return 0;
    Totals t = sum_entries(tmp.entries, tmp.count, -1);
    return goal_day(t.kcal, t.protein, g_targets.kcal, g_targets.protein);
}

static void compute_streak(void)
{
    g_streak_prev = streak_before(g_today, day_hit, NULL, 365);
}

static int streak_now(void)
{
    return g_streak_prev + (g_today_hit ? 1 : 0);
}

/* Re-checks today's goal after a change. A new goal day gets a chime. */
static void update_goal(bool announce)
{
    if (strcmp(g_day.date, g_today) != 0)
        return;
    Totals t = sum_entries(g_day.entries, g_day.count, -1);
    bool hit = goal_day(t.kcal, t.protein, g_targets.kcal, g_targets.protein) != 0;
    if (hit && !g_today_hit && announce) {
        sound_play(SND_GOAL);
        g_cheer = 180;
    }
    g_today_hit = hit;
}

static void load_day(const char *date)
{
    if (day_load(date, &g_day) != 0)
        status("Could not read the log for %s", date);
    rebuild_order();
    reset_list(TAB_TODAY);
    g_week_dirty = true;
    refresh_activity();
    update_goal(false);
}

static void save_day(void)
{
    if (day_save(&g_day) != 0)
        status("Could not save to the SD card");
    rebuild_order();
    g_week_dirty = true;
    update_goal(true);
}

static Meal meal_for_now(void)
{
    time_t now = time(NULL);
    int hour = localtime(&now)->tm_hour;
    if (hour < 11) return MEAL_BREAKFAST;
    if (hour < 16) return MEAL_LUNCH;
    if (hour < 21) return MEAL_DINNER;
    return MEAL_SNACKS;
}

static bool add_entry(const Entry *e)
{
    if (day_add(&g_day, e) != 0) {
        status("This day is full (%d entries)", CT_MAX_ENTRIES);
        return false;
    }
    return true;
}

/* Log a food to the current meal slot. */
static bool log_food_amount(const Food *food, Unit unit, float amount, float grams)
{
    Entry e;
    memset(&e, 0, sizeof e);
    e.food = *food;
    e.grams = grams;
    e.unit = unit;
    e.amount = amount;
    e.meal = g_slot;
    if (!add_entry(&e))
        return false;
    save_day();
    foods_touch(&g_cache, food);
    foods_save(&g_cache);
    sound_play(SND_POP);
    char amt[32];
    amount_text(&e, amt, sizeof amt);
    status("Added %s %.24s (%.0f kcal) to %s", amt, food->name, entry_totals(&e).kcal, meal_name(g_slot));
    return true;
}

static bool log_food_grams(const Food *food, float grams)
{
    return log_food_amount(food, UNIT_GRAMS, grams, grams);
}

/* Meal builder: add a food to the meal being built. */
static bool draft_add(const Entry *e)
{
    if (g_draft.count >= CT_MAX_MEAL_ITEMS) {
        status("A meal holds up to %d foods", CT_MAX_MEAL_ITEMS);
        return false;
    }
    g_draft.items[g_draft.count++] = *e;
    sound_play(SND_POP);
    status("Added %.24s to \"%.20s\" (%d foods)", e->food.name, g_draft.name, (int)g_draft.count);
    return true;
}

static void draft_add_food(const Food *food, Unit unit, float amount, float grams)
{
    Entry e;
    memset(&e, 0, sizeof e);
    e.food = *food;
    e.grams = grams;
    e.unit = unit;
    e.amount = amount;
    if (draft_add(&e)) {
        foods_touch(&g_cache, food);
        foods_save(&g_cache);
    }
}

/* Opens the "How much?" picker for a food. `current` is preselected. */
static void open_unit_picker(const Food *food, UnitMode mode, Unit current)
{
    g_unit_food = *food;
    g_unit_mode = mode;
    g_unit_n = 0;
    static const Unit order[] = {UNIT_SERVINGS, UNIT_GRAMS, UNIT_OUNCES, UNIT_ML, UNIT_CUPS};
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++)
        if (unit_grams(food, order[i]) > 0)
            g_unit_opts[g_unit_n++] = order[i];
    g_unit_sel = 0;
    for (int i = 0; i < g_unit_n; i++)
        if (g_unit_opts[i] == current)
            g_unit_sel = i;
    g_unit_picking = true;
}

/* "A" on a food: pick how to measure it, then log it (or add it to the
 * meal being built). */
static void use_food_ask(const Food *food)
{
    open_unit_picker(food, UNIT_FOR_LOG, food->serving_g > 0 ? UNIT_SERVINGS : UNIT_GRAMS);
}

static void use_food_grams(const Food *food, float grams)
{
    if (g_building)
        draft_add_food(food, UNIT_GRAMS, grams, grams);
    else
        log_food_grams(food, grams);
}

static Entry *selected_entry(void);

static void unit_chosen(void)
{
    Unit u = g_unit_opts[g_unit_sel];
    const Food *food = &g_unit_food;
    float per = unit_grams(food, u);
    Entry *edit = g_unit_mode == UNIT_FOR_EDIT ? selected_entry() : NULL;
    float amount;
    if (edit && edit->unit == u && edit->amount > 0)
        amount = edit->amount;
    else if (edit)
        amount = edit->grams / per;
    else if (u == UNIT_GRAMS)
        amount = food->serving_g > 0 ? food->serving_g : 100;
    else if (u == UNIT_ML)
        amount = 250;
    else
        amount = 1;
    amount = (float)(int)(amount * 10 + 0.5f) / 10;
    static const char *hints[UNIT_COUNT] = {"How many grams?", "How many servings? (1.5 works)", "How many ounces?",
                                            "How many ml?", "How many cups?"};
    g_unit_picking = false;
    if (!prompt_number(hints[u], amount, &amount) || amount <= 0)
        return;
    float grams = amount * per;
    if (edit) {
        edit->unit = u;
        edit->amount = amount;
        edit->grams = grams;
        save_day();
        status("Updated %.30s", edit->food.name);
    } else if (g_building) {
        draft_add_food(food, u, amount, grams);
    } else {
        log_food_amount(food, u, amount, grams);
    }
}

static void toggle_star(const Food *food, float grams)
{
    int rc = favs_toggle(&g_favs, food, grams);
    if (rc < 0) {
        status("Favorites are full (%d)", CT_MAX_FAVS);
        return;
    }
    favs_save(&g_favs);
    status(rc ? "Added %.30s to favorites" : "Removed %.30s from favorites", food->name);
}

static bool is_fav(const Food *food)
{
    return favs_find(&g_favs, food) >= 0;
}

static float current_weight(void)
{
    return series_latest(&g_weights, NULL);
}

static void compute_week(void)
{
    static DayLog tmp;
    for (int i = 0; i < WEEK; i++) {
        char *d = g_week_dates[i];
        if (date_add_days(g_day.date, i - (WEEK - 1), d) != 0) {
            d[0] = '\0';
            memset(&g_week[i], 0, sizeof g_week[i]);
            continue;
        }
        if (strcmp(d, g_day.date) == 0)
            g_week[i] = sum_entries(g_day.entries, g_day.count, -1);
        else if (day_load(d, &tmp) == 0)
            g_week[i] = sum_entries(tmp.entries, tmp.count, -1);
        else
            memset(&g_week[i], 0, sizeof g_week[i]);
    }
    g_week_dirty = false;
}

/* ---- selection helpers -------------------------------------------------- */
static int visible_rows(Tab t)
{
    if (t == TAB_TODAY) return 8;
    if (t == TAB_PROFILE) return PR_COUNT;
    if (t == TAB_MEALS && g_building) return 12;
    return 13;
}

static void move_sel(int *sel, int *scroll, int delta, int count, int rows)
{
    if (count <= 0) {
        *sel = 0;
        *scroll = 0;
        return;
    }
    *sel += delta;
    if (*sel < 0) *sel = count - 1;
    if (*sel >= count) *sel = 0;
    if (*sel < *scroll) *scroll = *sel;
    if (*sel >= *scroll + rows) *scroll = *sel - rows + 1;
}

static int list_count(Tab t)
{
    switch (t) {
    case TAB_TODAY: return (int)g_day.count;
    case TAB_SEARCH: return g_have_results ? g_result_count : (int)g_cache.count;
    case TAB_BRANDS: return g_brand_open >= 0 ? g_brand_count : (int)g_brands.count;
    case TAB_FAVS: return (int)g_favs.count;
    case TAB_MEALS: return g_building ? (int)g_draft.count : (int)g_meals.count;
    default: return g_page == PAGE_PROFILE ? PR_COUNT : 0;
    }
}

static const Food *search_food(int i)
{
    return g_have_results ? &g_results[i] : &g_cache.items[i];
}

static Entry *selected_entry(void)
{
    if (g_day.count == 0)
        return NULL;
    return &g_day.entries[g_order[g_sel[TAB_TODAY]]];
}

/* ---- network ------------------------------------------------------------ */
static void run_search(void)
{
    char url[512];
    char err[96] = "";
    g_result_count = 0;
    for (int ep = 0; ep < OFF_ENDPOINT_COUNT; ep++) {
        if (off_search_url(ep, g_query, url, sizeof url) != 0) {
            status("Search text is too long");
            return;
        }
        char *body = NULL;
        if (net_get(url, &body, err, sizeof err) != 0)
            continue;
        int n = off_parse_search(body, g_results, OFF_PAGE_SIZE);
        free(body);
        if (n < 0) {
            snprintf(err, sizeof err, "unexpected reply");
            continue;
        }
        g_result_count = n;
        g_have_results = true;
        if (n > 0)
            status("%d results for \"%.30s\"", n, g_query);
        else
            status("No results for \"%.30s\"", g_query);
        return;
    }
    status("Search failed: %s", err);
}

static bool same_text(const char *a, const char *b)
{
    return strlen(a) == strlen(b) && off_text_contains(a, b);
}

/* If the products found carry a different brand name than the one typed
 * (a fuzzy match, like "goodless" finding "Goodles"), rename the tile. */
static void fix_brand_name(void)
{
    const char *found = g_brand_foods[0].brand;
    const char *typed = g_brands.names[g_brand_open];
    if (!found[0] || off_text_contains(found, typed))
        return;
    char real[CT_BRAND_LEN];
    ct_strlcpy(real, found, sizeof real);
    char *comma = strchr(real, ',');
    if (comma)
        *comma = '\0';
    size_t len = strlen(real);
    while (len > 0 && real[len - 1] == ' ')
        real[--len] = '\0';
    if (len == 0)
        return;
    for (size_t i = 0; i < g_brands.count; i++)
        if ((int)i != g_brand_open && same_text(g_brands.names[i], real))
            return; /* already followed under the real name */
    ct_strlcpy(g_brands.names[g_brand_open], real, CT_BRAND_LEN);
    brands_save(&g_brands);
}

static void fetch_brand(void)
{
    char brand[CT_BRAND_LEN];
    ct_strlcpy(brand, g_brands.names[g_brand_open], sizeof brand);
    char url[512];
    char err[96] = "";
    bool answered = false; /* a server replied properly, even with no products */
    for (int ep = 0; ep < OFF_BRAND_ENDPOINTS; ep++) {
        if (off_brand_url(ep, brand, url, sizeof url) != 0)
            break;
        char *body = NULL;
        if (net_get(url, &body, err, sizeof err) != 0)
            continue;
        int n = off_parse_brand(body, brand, g_brand_foods, MAX_BRAND_FOODS);
        free(body);
        if (n < 0) {
            snprintf(err, sizeof err, "unexpected reply");
            continue;
        }
        answered = true;
        if (n == 0)
            continue; /* try a looser search */
        g_brand_count = n;
        fix_brand_name();
        brand_cache_save(g_brands.names[g_brand_open], g_brand_foods, n);
        if (same_text(brand, g_brands.names[g_brand_open]))
            status("%d %.20s products", n, brand);
        else
            status("Found %d products as \"%.20s\"", n, g_brands.names[g_brand_open]);
        return;
    }
    g_brand_count = 0;
    if (answered)
        status("No products found for %.20s. Check the spelling", brand);
    else
        status("Could not load %.20s: %s", brand, err);
}

static void open_brand(int index)
{
    g_brand_open = index;
    g_brand_sel = 0;
    g_brand_scroll = 0;
    int n = brand_cache_load(g_brands.names[index], g_brand_foods, MAX_BRAND_FOODS);
    if (n > 0) {
        g_brand_count = n;
        status("%d saved products. Y to refresh", n);
    } else if (g_net_ok) {
        g_brand_count = 0;
        g_pending = PEND_BRAND;
        status("Loading %.24s...", g_brands.names[index]);
    } else {
        g_brand_count = 0;
        status("No network, and nothing saved for this brand yet");
    }
}

/* ---- actions ------------------------------------------------------------ */
/* Name and macros, no grams. Goes into the day, or into the meal being built. */
static void quick_entry(void)
{
    Entry e;
    memset(&e, 0, sizeof e);
    float kcal = 0, p = 0, c = 0, f = 0;
    if (!prompt_text("Name (e.g. Abuela's arroz con pollo)", NULL, e.food.name, sizeof e.food.name)) return;
    if (!prompt_number("Calories", kcal, &kcal)) return;
    if (!prompt_number("Protein (g)", p, &p)) return;
    if (!prompt_number("Carbs (g)", c, &c)) return;
    if (!prompt_number("Fat (g)", f, &f)) return;
    e.food.kcal = kcal;
    e.food.protein = p;
    e.food.carbs = c;
    e.food.fat = f;
    e.grams = 100; /* values are totals: 100 g of "per 100 g" */
    e.quick = 1;
    if (g_building) {
        draft_add(&e);
        return;
    }
    e.meal = g_slot;
    if (!add_entry(&e))
        return;
    save_day();
    sound_play(SND_POP);
    status("Quick added %.0f kcal to %s", kcal, meal_name(g_slot));
}

static void log_saved_meal(const SavedMeal *m, float factor)
{
    int added = 0;
    for (size_t i = 0; i < m->count; i++) {
        Entry e = m->items[i];
        e.meal = g_slot;
        if (e.quick) {
            e.food.kcal *= factor;
            e.food.protein *= factor;
            e.food.carbs *= factor;
            e.food.fat *= factor;
        } else {
            e.grams *= factor;
            e.amount *= factor;
        }
        if (!add_entry(&e))
            break;
        added++;
    }
    save_day();
    if (added > 0)
        sound_play(SND_POP);
    status("Logged \"%.24s\" (%d foods) to %s", m->name, added, meal_name(g_slot));
}

static void switch_tab(Tab t)
{
    if (t != g_tab)
        sound_play(SND_TAP);
    g_picking = false;
    g_show_micros = false;
    if (t == TAB_PROFILE && g_tab != TAB_PROFILE)
        refresh_activity();
    g_tab = t;
    if (g_status[0] == 'L' && strncmp(g_status, "Loading", 7) == 0)
        g_status[0] = '\0';
}

/* ---- water ------------------------------------------------------------- */
static int water_goal(void)
{
    return g_profile.water_goal > 0 ? g_profile.water_goal : 8;
}

static int water_glasses(void)
{
    return (int)(series_get(&g_water, g_day.date) + 0.5f);
}

static void change_water(int delta)
{
    int n = water_glasses() + delta;
    if (n < 0)
        return;
    if (n > 40)
        n = 40;
    series_set(&g_water, g_day.date, (float)n);
    if (series_save(&g_water, WATER_FILE) != 0) {
        status("Could not save water");
        return;
    }
    if (delta > 0 && n == water_goal()) {
        sound_play(SND_GOAL);
        g_cheer = 120;
        status("Water goal reached: %d glasses!", n);
    } else {
        sound_play(SND_WATER);
        status("Water: %d of %d glasses", n, water_goal());
    }
}

/* ---- barcode scanning --------------------------------------------------- */
static void start_scan(void)
{
    if (!g_net_ok) {
        status("Scanning needs the internet to look foods up");
        return;
    }
    if (scanner_start() != 0) {
        status("Could not start the camera");
        return;
    }
    g_scanning = true;
    status("Point the outer camera at a barcode");
}

static void stop_scan(void)
{
    scanner_stop();
    g_scanning = false;
}

/* Looks up g_barcode on Open Food Facts and shows it in Search. */
static void lookup_product(void)
{
    char url[256], err[96] = "";
    if (off_product_url(g_barcode, url, sizeof url) != 0) {
        status("That barcode doesn't look right");
        return;
    }
    char *body = NULL;
    if (net_get(url, &body, err, sizeof err) != 0) {
        status("Lookup failed: %s", err);
        return;
    }
    Food f;
    int rc = off_parse_product(body, &f);
    free(body);
    if (rc < 0) {
        status("Lookup failed: unexpected reply");
        return;
    }
    if (rc == 0) {
        status("%s isn't in Open Food Facts yet. Try Quick add", g_barcode);
        return;
    }
    g_results[0] = f;
    g_result_count = 1;
    g_have_results = true;
    snprintf(g_query, sizeof g_query, "%s", g_barcode);
    switch_tab(TAB_SEARCH);
    reset_list(TAB_SEARCH);
    status("Found it! A to %s %.28s", g_building ? "add" : "log", f.name);
}

static void start_meal(void)
{
    char name[CT_NAME_LEN] = "";
    if (!prompt_text("Name your meal (e.g. Protein breakfast)", NULL, name, sizeof name))
        return;
    memset(&g_draft, 0, sizeof g_draft);
    ct_strlcpy(g_draft.name, name, sizeof g_draft.name);
    g_building = true;
    reset_list(TAB_MEALS);
    switch_tab(TAB_SEARCH);
    status("Pick foods with A. Favs and Brands work too");
}

static void finish_meal(void)
{
    if (g_draft.count == 0) {
        status("Add at least one food first");
        return;
    }
    if (meals_add(&g_meals, &g_draft) != 0) {
        status("Saved meals are full (%d)", CT_MAX_SAVED_MEALS);
        return;
    }
    meals_save(&g_meals);
    status("Saved \"%.24s\" with %d foods", g_draft.name, (int)g_draft.count);
    g_building = false;
    reset_list(TAB_MEALS);
    g_sel[TAB_MEALS] = (int)g_meals.count - 1;
    move_sel(&g_sel[TAB_MEALS], &g_scroll[TAB_MEALS], 0, (int)g_meals.count, visible_rows(TAB_MEALS));
}

static void log_weight(void)
{
    float kg = current_weight();
    if (!prompt_number("Weight today (kg)", kg > 0 ? kg : 50, &kg) || kg <= 0)
        return;
    series_set(&g_weights, g_today, kg);
    status(series_save(&g_weights, WEIGHTS_FILE) == 0 ? "Weight saved: %.1f kg" : "Could not save (%.1f kg)", kg);
}

static void edit_target(const char *hint, float *value)
{
    float v = *value;
    if (prompt_number(hint, v, &v)) {
        *value = v;
        status(targets_save(&g_targets) == 0 ? "Targets saved" : "Could not save targets");
        compute_streak();
        update_goal(false);
    }
}

static void save_profile(void)
{
    status(profile_save(&g_profile) == 0 ? "Profile saved" : "Could not save the profile");
}

static void edit_profile_row(ProfileRow row)
{
    UserProfile *p = &g_profile;
    float v;
    switch (row) {
    case PR_AGE:
        v = (float)p->age;
        if (prompt_number("Age (years)", v, &v) && v < 130) {
            p->age = (int)(v + 0.5f);
            save_profile();
        }
        break;
    case PR_SEX:
        p->sex = p->sex == SEX_MALE ? SEX_FEMALE : SEX_MALE;
        save_profile();
        break;
    case PR_WEIGHT:
        log_weight();
        break;
    case PR_HEIGHT:
        v = p->height_cm;
        if (prompt_number("Height (cm)", v > 0 ? v : 160, &v) && v < 260) {
            p->height_cm = v;
            save_profile();
        }
        break;
    case PR_BMI:
        status("BMI is worked out from your weight and height");
        break;
    case PR_BODY_FAT:
        v = p->body_fat;
        if (prompt_number("Body fat (%), 0 if you don't know", v, &v) && v < 70) {
            p->body_fat = v;
            save_profile();
        }
        break;
    case PR_ACTIVITY:
        p->activity = (Activity)((p->activity + 1) % ACTIVITY_COUNT);
        save_profile();
        break;
    case PR_WATER:
        v = (float)water_goal();
        if (prompt_number("Water goal (glasses of 250 ml)", v, &v) && v >= 1 && v <= 30) {
            p->water_goal = (int)(v + 0.5f);
            save_profile();
        }
        break;
    case PR_SOUNDS:
        p->sounds_off = !p->sounds_off;
        sound_enable(!p->sounds_off);
        save_profile();
        sound_play(SND_TAP);
        break;
    case PR_KCAL: edit_target("Daily calories", &g_targets.kcal); break;
    case PR_PROTEIN: edit_target("Daily protein (g)", &g_targets.protein); break;
    case PR_CARBS: edit_target("Daily carbs (g)", &g_targets.carbs); break;
    case PR_FAT: edit_target("Daily fat (g)", &g_targets.fat); break;
    default: break;
    }
}

static float weight_for_day(void)
{
    float kg = series_latest(&g_weights, g_day.date);
    return kg > 0 ? kg : current_weight();
}

static void add_workout(const WorkoutType *type)
{
    Workout w;
    memset(&w, 0, sizeof w);
    if (type->met > 0) {
        float kg = weight_for_day();
        if (kg <= 0) {
            status("Log your weight in Profile first");
            return;
        }
        float minutes = 45;
        if (!prompt_number("How many minutes?", minutes, &minutes) || minutes <= 0)
            return;
        ct_strlcpy(w.name, type->name, sizeof w.name);
        w.minutes = minutes;
        w.kcal = workout_kcal(type->met, kg, minutes);
    } else {
        if (!prompt_text("Workout name (e.g. Boxing)", NULL, w.name, sizeof w.name))
            return;
        float kcal = 0;
        if (!prompt_number("Calories burned", kcal, &kcal) || kcal <= 0)
            return;
        w.kcal = kcal;
    }
    if (workouts_add(&g_workouts, &w) != 0) {
        status("Up to %d workouts a day", CT_MAX_WORKOUTS);
        return;
    }
    g_picking = false;
    g_workout_sel = (int)g_workouts.count - 1;
    if (workouts_save(&g_workouts) != 0)
        status("Could not save the workout");
    else
        status("Added %.20s: %.0f kcal", w.name, w.kcal);
}

static void shift_day(int days)
{
    char date[CT_DATE_LEN];
    if (date_add_days(g_day.date, days, date) == 0)
        load_day(date);
}

static void do_action(Action a)
{
    Tab t = g_tab;
    int count = list_count(t);
    int *sel = &g_sel[t];

    switch (a) {
    case ACT_NONE:
        break;
    case ACT_GO_SEARCH:
        switch_tab(TAB_SEARCH);
        break;
    case ACT_GO_MEALS:
        switch_tab(TAB_MEALS);
        break;
    case ACT_MEAL_PREV:
        g_slot = (Meal)((g_slot + MEAL_COUNT - 1) % MEAL_COUNT);
        break;
    case ACT_MEAL_NEXT:
        g_slot = (Meal)((g_slot + 1) % MEAL_COUNT);
        break;
    case ACT_PREV_DAY: shift_day(-1); break;
    case ACT_NEXT_DAY: shift_day(1); break;
    case ACT_PREV_WEEK: shift_day(-WEEK); break;
    case ACT_SCAN: start_scan(); break;
    case ACT_SCAN_CANCEL:
        stop_scan();
        status("Scan cancelled");
        break;
    case ACT_WATER_ADD: change_water(1); break;
    case ACT_WATER_REMOVE: change_water(-1); break;
    case ACT_MICROS: g_show_micros = true; break;
    case ACT_MICROS_BACK: g_show_micros = false; break;
    case ACT_UNIT_CHOOSE:
        if (g_unit_picking)
            unit_chosen();
        break;
    case ACT_UNIT_CANCEL: g_unit_picking = false; break;
    case ACT_NEXT_WEEK: shift_day(WEEK); break;
    case ACT_TODAY:
        load_day(g_today);
        break;
    case ACT_QUICK:
        quick_entry();
        break;
    case ACT_NEW_MEAL:
        start_meal();
        break;
    case ACT_SAVE_DRAFT:
        finish_meal();
        break;
    case ACT_CANCEL_DRAFT:
        g_building = false;
        reset_list(TAB_MEALS);
        status("Meal not saved");
        break;
    case ACT_PAGE_PROFILE: g_page = PAGE_PROFILE; break;
    case ACT_PAGE_ACTIVITY: g_page = PAGE_ACTIVITY; break;
    case ACT_PAGE_REPORT: g_page = PAGE_REPORT; break;
    case ACT_PAGE_CHARTS: g_page = PAGE_CHARTS; break;
    case ACT_EDIT_ROW:
        if (t == TAB_PROFILE && g_page == PAGE_PROFILE)
            edit_profile_row((ProfileRow)*sel);
        break;
    case ACT_LOG_WEIGHT:
        log_weight();
        break;
    case ACT_ADD_WORKOUT:
        g_picking = true;
        g_pick_sel = 0;
        break;
    case ACT_PICK_CANCEL:
        g_picking = false;
        break;
    case ACT_PICK_CHOOSE:
        if (g_picking)
            add_workout(&WORKOUT_TYPES[g_pick_sel]);
        break;
    case ACT_REMOVE_WORKOUT:
        if (g_workouts.count > 0) {
            status("Removed %.24s", g_workouts.items[g_workout_sel].name);
            workouts_remove(&g_workouts, (size_t)g_workout_sel);
            workouts_save(&g_workouts);
            if (g_workout_sel >= (int)g_workouts.count && g_workout_sel > 0)
                g_workout_sel--;
        }
        break;
    case ACT_DELETE:
        if (t == TAB_TODAY && count > 0) {
            Entry *e = selected_entry();
            status("Removed %.30s", e->food.name);
            day_remove(&g_day, g_order[*sel]);
            save_day();
            if (*sel >= (int)g_day.count) move_sel(sel, &g_scroll[t], -1, (int)g_day.count, visible_rows(t));
        } else if (t == TAB_MEALS && g_building && count > 0) {
            status("Took %.30s out", g_draft.items[*sel].food.name);
            memmove(&g_draft.items[*sel], &g_draft.items[*sel + 1],
                    (g_draft.count - (size_t)*sel - 1) * sizeof(Entry));
            g_draft.count--;
            if (*sel >= (int)g_draft.count) move_sel(sel, &g_scroll[t], -1, (int)g_draft.count, visible_rows(t));
        } else if (t == TAB_MEALS && count > 0) {
            status("Deleted meal \"%.24s\"", g_meals.items[*sel].name);
            meals_remove(&g_meals, (size_t)*sel);
            meals_save(&g_meals);
            if (*sel >= (int)g_meals.count) move_sel(sel, &g_scroll[t], -1, (int)g_meals.count, visible_rows(t));
        }
        break;
    case ACT_EDIT_GRAMS:
        if (t == TAB_TODAY && count > 0) {
            Entry *e = selected_entry();
            if (e->quick) {
                status("Quick entries have no grams: remove and re-add");
                break;
            }
            open_unit_picker(&e->food, UNIT_FOR_EDIT, e->amount > 0 ? e->unit : UNIT_GRAMS);
        } else if (t == TAB_FAVS && count > 0) {
            Favorite *f = &g_favs.items[*sel];
            float grams = f->grams;
            if (prompt_number("Usual grams for this favorite", grams, &grams) && grams > 0) {
                f->grams = grams;
                favs_save(&g_favs);
                status("%.30s now logs %.0f g", f->food.name, grams);
            }
        }
        break;
    case ACT_STAR:
        if (t == TAB_TODAY && count > 0) {
            Entry *e = selected_entry();
            if (e->quick)
                status("Quick entries can't be favorites. Put it in a meal instead");
            else
                toggle_star(&e->food, e->grams);
        } else if (t == TAB_SEARCH && count > 0) {
            const Food *f = search_food(*sel);
            toggle_star(f, f->serving_g);
        } else if (t == TAB_BRANDS && g_brand_open >= 0 && g_brand_count > 0) {
            const Food *f = &g_brand_foods[g_brand_sel];
            toggle_star(f, f->serving_g);
        } else if (t == TAB_FAVS && count > 0) {
            toggle_star(&g_favs.items[*sel].food, 0);
            if (*sel >= (int)g_favs.count) move_sel(sel, &g_scroll[t], -1, (int)g_favs.count, visible_rows(t));
        }
        break;
    case ACT_LOG:
        if (t == TAB_SEARCH && count > 0) {
            use_food_ask(search_food(*sel));
        } else if (t == TAB_BRANDS && g_brand_open >= 0 && g_brand_count > 0) {
            use_food_ask(&g_brand_foods[g_brand_sel]);
        } else if (t == TAB_FAVS && count > 0) {
            Favorite f = g_favs.items[*sel];
            use_food_grams(&f.food, f.grams);
        } else if (t == TAB_MEALS && !g_building && count > 0) {
            log_saved_meal(&g_meals.items[*sel], 1.0f);
        }
        break;
    case ACT_LOG_GRAMS:
        if (t == TAB_FAVS && count > 0) {
            Favorite f = g_favs.items[*sel];
            float grams = f.grams;
            if (prompt_number("Grams", grams, &grams) && grams > 0)
                use_food_grams(&f.food, grams);
        }
        break;
    case ACT_LOG_SCALED:
        if (t == TAB_MEALS && !g_building && count > 0) {
            float factor = 1.0f;
            if (prompt_number("How much? 1 = normal, 0.5 = half, 2 = double", factor, &factor) &&
                factor > 0)
                log_saved_meal(&g_meals.items[*sel], factor);
        }
        break;
    case ACT_SEARCH:
        if (!g_net_ok) {
            status("No network: online search is off");
        } else if (prompt_text("Search foods (e.g. greek yogurt)", g_query[0] ? g_query : NULL, g_query,
                               sizeof g_query)) {
            status("Searching for \"%.30s\"...", g_query);
            g_pending = PEND_SEARCH;
            reset_list(TAB_SEARCH);
        }
        break;
    case ACT_ADD_BRAND: {
        char name[CT_BRAND_LEN] = "";
        if (!prompt_text("Brand name (e.g. Quest, Barebells)", NULL, name, sizeof name))
            break;
        int rc = brands_add(&g_brands, name);
        if (rc < 0) {
            status("You can follow up to %d brands", CT_MAX_BRANDS);
        } else {
            brands_save(&g_brands);
            status(rc ? "Added %.24s" : "%.24s is already in your brands", name);
        }
        break;
    }
    case ACT_REMOVE_BRAND:
        if (g_brand_open < 0 && count > 0) {
            status("Removed %.24s", g_brands.names[*sel]);
            brands_remove(&g_brands, (size_t)*sel);
            brands_save(&g_brands);
            if (*sel >= (int)g_brands.count && *sel > 0) (*sel)--;
        }
        break;
    case ACT_OPEN_BRAND:
        if (g_brand_open < 0 && count > 0)
            open_brand(*sel);
        break;
    case ACT_REFRESH:
        if (g_brand_open >= 0) {
            if (!g_net_ok) {
                status("No network");
            } else {
                g_pending = PEND_BRAND;
                status("Loading %.24s...", g_brands.names[g_brand_open]);
            }
        }
        break;
    case ACT_BACK:
        if (t == TAB_BRANDS && g_brand_open >= 0) {
            g_brand_open = -1;
        } else if (t == TAB_SEARCH && g_have_results) {
            g_have_results = false;
            reset_list(TAB_SEARCH);
        }
        break;
    }
}

/* Button shortcuts, per tab. Returns false to quit. */
static bool handle_keys(u32 down)
{
    if (down & KEY_START)
        return false;
    if (g_scanning) {
        if (down & (KEY_B | KEY_START))
            do_action(ACT_SCAN_CANCEL);
        return true;
    }
    if (g_unit_picking) {
        int dummy = 0;
        if (down & KEY_DUP) move_sel(&g_unit_sel, &dummy, -1, g_unit_n, g_unit_n);
        if (down & KEY_DDOWN) move_sel(&g_unit_sel, &dummy, 1, g_unit_n, g_unit_n);
        if (down & KEY_A) do_action(ACT_UNIT_CHOOSE);
        if (down & KEY_B) do_action(ACT_UNIT_CANCEL);
        return true;
    }
    if (down & KEY_L) {
        switch_tab((Tab)((g_tab + TAB_COUNT - 1) % TAB_COUNT));
        return true;
    }
    if (down & KEY_R) {
        switch_tab((Tab)((g_tab + 1) % TAB_COUNT));
        return true;
    }

    Tab t = g_tab;
    int count = list_count(t);

    /* Brand tiles: a 3-column grid */
    if (t == TAB_BRANDS && g_brand_open < 0) {
        int *sel = &g_sel[t];
        if ((down & KEY_DRIGHT) && *sel + 1 < count) (*sel)++;
        if ((down & KEY_DLEFT) && *sel > 0) (*sel)--;
        if ((down & KEY_DDOWN) && *sel + 3 < count) *sel += 3;
        if ((down & KEY_DUP) && *sel >= 3) *sel -= 3;
        if (down & KEY_A) do_action(ACT_OPEN_BRAND);
        if (down & KEY_Y) do_action(ACT_ADD_BRAND);
        if (down & KEY_X) do_action(ACT_REMOVE_BRAND);
        return true;
    }

    /* Profile: Left/Right change page */
    if (t == TAB_PROFILE) {
        if (g_picking) {
            int dummy = 0;
            if (down & KEY_DUP) move_sel(&g_pick_sel, &dummy, -1, WORKOUT_TYPE_COUNT, WORKOUT_TYPE_COUNT);
            if (down & KEY_DDOWN) move_sel(&g_pick_sel, &dummy, 1, WORKOUT_TYPE_COUNT, WORKOUT_TYPE_COUNT);
            if (down & KEY_A) do_action(ACT_PICK_CHOOSE);
            if (down & KEY_B) do_action(ACT_PICK_CANCEL);
            return true;
        }
        if (down & KEY_DLEFT) g_page = (Page)((g_page + PAGE_COUNT - 1) % PAGE_COUNT);
        if (down & KEY_DRIGHT) g_page = (Page)((g_page + 1) % PAGE_COUNT);
        if (g_page == PAGE_ACTIVITY) {
            int dummy = 0, n = (int)g_workouts.count;
            if (down & KEY_DUP) move_sel(&g_workout_sel, &dummy, -1, n, n);
            if (down & KEY_DDOWN) move_sel(&g_workout_sel, &dummy, 1, n, n);
            if (down & KEY_A) do_action(ACT_ADD_WORKOUT);
            if (down & KEY_X) do_action(ACT_REMOVE_WORKOUT);
            if (down & KEY_Y) do_action(ACT_PREV_DAY);
            if (down & KEY_B) do_action(ACT_TODAY);
        } else if (g_page == PAGE_PROFILE) {
            if (down & KEY_DUP) move_sel(&g_sel[t], &g_scroll[t], -1, PR_COUNT, PR_COUNT);
            if (down & KEY_DDOWN) move_sel(&g_sel[t], &g_scroll[t], 1, PR_COUNT, PR_COUNT);
            if (down & KEY_A) do_action(ACT_EDIT_ROW);
            if (down & KEY_Y) do_action(ACT_LOG_WEIGHT);
        } else if (g_page == PAGE_REPORT) {
            if (down & KEY_Y) do_action(ACT_PREV_DAY);
            if (down & KEY_X) do_action(ACT_NEXT_DAY);
            if (down & KEY_B) do_action(ACT_TODAY);
        } else {
            if (down & KEY_Y) do_action(ACT_PREV_WEEK);
            if (down & KEY_X) do_action(ACT_NEXT_WEEK);
            if (down & KEY_B) do_action(ACT_TODAY);
        }
        return true;
    }

    int *sel = (t == TAB_BRANDS) ? &g_brand_sel : &g_sel[t];
    int *scroll = (t == TAB_BRANDS) ? &g_brand_scroll : &g_scroll[t];
    if (down & KEY_DUP) move_sel(sel, scroll, -1, count, visible_rows(t));
    if (down & KEY_DDOWN) move_sel(sel, scroll, 1, count, visible_rows(t));

    if (t == TAB_TODAY && g_show_micros) {
        if (down & KEY_DLEFT) do_action(ACT_PREV_DAY);
        if (down & KEY_DRIGHT) do_action(ACT_NEXT_DAY);
        if (down & KEY_B) do_action(ACT_MICROS_BACK);
        return true;
    }
    if (t == TAB_TODAY) {
        if (down & KEY_ZR) do_action(ACT_WATER_ADD);
        if (down & KEY_ZL) do_action(ACT_WATER_REMOVE);
        if (down & KEY_DLEFT) do_action(ACT_PREV_DAY);
        if (down & KEY_DRIGHT) do_action(ACT_NEXT_DAY);
        if (down & KEY_A) do_action(ACT_GO_SEARCH);
        if (down & KEY_B) do_action(ACT_TODAY);
        if (down & KEY_X) do_action(ACT_DELETE);
        if (down & KEY_Y) do_action(ACT_EDIT_GRAMS);
        if (down & KEY_SELECT) switch_tab(TAB_PROFILE);
        return true;
    }

    if (t == TAB_MEALS && g_building) {
        if (down & KEY_A) do_action(ACT_SAVE_DRAFT);
        if (down & KEY_B) do_action(ACT_CANCEL_DRAFT);
        if (down & KEY_Y) do_action(ACT_GO_SEARCH);
        if (down & KEY_X) do_action(ACT_DELETE);
        if (down & KEY_SELECT) do_action(ACT_QUICK);
        return true;
    }

    if (!g_building) {
        if (down & KEY_DLEFT) do_action(ACT_MEAL_PREV);
        if (down & KEY_DRIGHT) do_action(ACT_MEAL_NEXT);
    }
    if (down & KEY_A) do_action(ACT_LOG);
    if (down & KEY_B) do_action(ACT_BACK);
    switch (t) {
    case TAB_SEARCH:
        if (down & KEY_Y) do_action(ACT_SEARCH);
        if (down & KEY_X) do_action(ACT_STAR);
        if (down & KEY_SELECT) do_action(ACT_SCAN);
        break;
    case TAB_BRANDS:
        if (down & KEY_Y) do_action(ACT_REFRESH);
        if (down & KEY_X) do_action(ACT_STAR);
        break;
    case TAB_FAVS:
        if (down & KEY_Y) do_action(ACT_LOG_GRAMS);
        if (down & KEY_X) do_action(ACT_STAR);
        if (down & KEY_SELECT) do_action(ACT_EDIT_GRAMS);
        break;
    case TAB_MEALS:
        if (down & KEY_Y) do_action(ACT_LOG_SCALED);
        if (down & KEY_X) do_action(ACT_DELETE);
        if (down & KEY_SELECT) do_action(ACT_NEW_MEAL);
        break;
    default:
        break;
    }
    return true;
}

/* ---- bottom screen layout (also used for touch hit-testing) ------------- */
#define TAB_Y 204.0f
#define BTN_Y 110.0f
#define BTN_H 26.0f
#define SLOT_Y 183.0f

static void layout_buttons(void)
{
    g_button_count = 0;
    float w3 = (BOT_W - 8 * 4) / 3; /* three per row */
    float x0 = 8, x1 = 8 + w3 + 8, x2 = 8 + 2 * (w3 + 8);
    float r0 = BTN_Y, r1 = BTN_Y + BTN_H + 5;
    const char *add = g_building ? "A  Add to meal" : "A  Log";
    float w4 = (BOT_W - 8 * 5) / 4; /* four per row */

    if (g_scanning) {
        add_button(x1, r1, w3, BTN_H, "B  Cancel", C_PANEL2, ACT_SCAN_CANCEL);
        return;
    }
    if (g_unit_picking) {
        add_button(x0, r1, w3, BTN_H, "A  Choose", C_GRAD, ACT_UNIT_CHOOSE);
        add_button(x1, r1, w3, BTN_H, "B  Cancel", C_PANEL2, ACT_UNIT_CANCEL);
        return;
    }

    switch (g_tab) {
    case TAB_TODAY:
        if (g_show_micros) {
            add_button(x0, r1, w3, BTN_H, "< Day", C_PANEL2, ACT_PREV_DAY);
            add_button(x1, r1, w3, BTN_H, "B  Back", C_GRAD, ACT_MICROS_BACK);
            add_button(x2, r1, w3, BTN_H, "Day >", C_PANEL2, ACT_NEXT_DAY);
            break;
        }
        add_button(8, r0, w4, BTN_H, "Quick add", C_GRAD, ACT_QUICK);
        add_button(8 + (w4 + 8), r0, w4, BTN_H, "A  Add food", C_GRAD, ACT_GO_SEARCH);
        add_button(8 + 2 * (w4 + 8), r0, w4, BTN_H, "Scan", C_GRAD, ACT_SCAN);
        add_button(8 + 3 * (w4 + 8), r0, w4, BTN_H, "Fav it", C_GRAD, ACT_STAR);
        add_button(8, r1, w4, BTN_H, "< Day", C_PANEL2, ACT_PREV_DAY);
        add_button(8 + (w4 + 8), r1, w4 - 26, BTN_H, "+ Water", C_GRAD, ACT_WATER_ADD);
        add_button(8 + (w4 + 8) + w4 - 22, r1, 22, BTN_H, "-", C_PANEL2, ACT_WATER_REMOVE);
        add_button(8 + 2 * (w4 + 8), r1, w4, BTN_H, "All micros", C_GRAD, ACT_MICROS);
        add_button(8 + 3 * (w4 + 8), r1, w4, BTN_H, "Day >", C_PANEL2, ACT_NEXT_DAY);
        break;
    case TAB_SEARCH:
        add_button(x0, r0, w3, BTN_H, "Y  Search", C_GRAD, ACT_SEARCH);
        add_button(x1, r0, w3, BTN_H, add, C_GRAD, ACT_LOG);
        add_button(x2, r0, w3, BTN_H, "X  Fav", C_GRAD, ACT_STAR);
        if (g_have_results)
            add_button(x0, r1, w3, BTN_H, "B  Recent", C_PANEL2, ACT_BACK);
        add_button(x1, r1, w3, BTN_H, "Scan barcode", C_GRAD, ACT_SCAN);
        break;
    case TAB_BRANDS:
        if (g_brand_open < 0) {
            add_button(x0, r0, w3, BTN_H, "Y  Add brand", C_GRAD, ACT_ADD_BRAND);
            add_button(x1, r0, w3, BTN_H, "A  Open", C_GRAD, ACT_OPEN_BRAND);
            add_button(x2, r0, w3, BTN_H, "X  Remove", C_PANEL2, ACT_REMOVE_BRAND);
        } else {
            add_button(x0, r0, w3, BTN_H, add, C_GRAD, ACT_LOG);
            add_button(x1, r0, w3, BTN_H, "X  Fav", C_GRAD, ACT_STAR);
            add_button(x2, r0, w3, BTN_H, "Y  Refresh", C_GRAD, ACT_REFRESH);
            add_button(x0, r1, w3, BTN_H, "B  Brands", C_PANEL2, ACT_BACK);
        }
        break;
    case TAB_FAVS:
        add_button(x0, r0, w3, BTN_H, add, C_GRAD, ACT_LOG);
        add_button(x1, r0, w3, BTN_H, "Y  Other grams", C_GRAD, ACT_LOG_GRAMS);
        add_button(x2, r0, w3, BTN_H, "X  Unfav", C_PANEL2, ACT_STAR);
        add_button(x0, r1, w3, BTN_H, "Usual grams", C_PANEL2, ACT_EDIT_GRAMS);
        break;
    case TAB_MEALS:
        if (g_building) {
            add_button(x0, r0, w3, BTN_H, "Y  Add foods", C_GRAD, ACT_GO_SEARCH);
            add_button(x1, r0, w3, BTN_H, "Quick item", C_GRAD, ACT_QUICK);
            add_button(x2, r0, w3, BTN_H, "X  Take out", C_PANEL2, ACT_DELETE);
            add_button(x0, r1, w3, BTN_H, "A  Save meal", C_GRAD, ACT_SAVE_DRAFT);
            add_button(x1, r1, w3, BTN_H, "B  Cancel", C_PANEL2, ACT_CANCEL_DRAFT);
        } else {
            add_button(x0, r0, w3, BTN_H, "+ New meal", C_GRAD, ACT_NEW_MEAL);
            add_button(x1, r0, w3, BTN_H, "A  Log meal", C_GRAD, ACT_LOG);
            add_button(x2, r0, w3, BTN_H, "Y  Half/double", C_GRAD, ACT_LOG_SCALED);
            add_button(x0, r1, w3, BTN_H, "X  Delete", C_PANEL2, ACT_DELETE);
        }
        break;
    case TAB_PROFILE: {
        if (g_picking) {
            add_button(x0, r1, w3, BTN_H, "A  Choose", C_GRAD, ACT_PICK_CHOOSE);
            add_button(x1, r1, w3, BTN_H, "B  Cancel", C_PANEL2, ACT_PICK_CANCEL);
            break;
        }
        static const Action pages[PAGE_COUNT] = {ACT_PAGE_PROFILE, ACT_PAGE_ACTIVITY, ACT_PAGE_REPORT,
                                                 ACT_PAGE_CHARTS};
        for (int i = 0; i < PAGE_COUNT; i++)
            add_button(8 + i * (w4 + 8), r0, w4, BTN_H, PAGE_NAMES[i], i == (int)g_page ? C_GRAD : C_PANEL2,
                       pages[i]);
        if (g_page == PAGE_ACTIVITY) {
            add_button(8, r1, w4, BTN_H, "Y  < Day", C_PANEL2, ACT_PREV_DAY);
            add_button(8 + (w4 + 8), r1, w4, BTN_H, "A  Workout", C_GRAD, ACT_ADD_WORKOUT);
            add_button(8 + 2 * (w4 + 8), r1, w4, BTN_H, "X  Remove", C_PANEL2, ACT_REMOVE_WORKOUT);
            add_button(8 + 3 * (w4 + 8), r1, w4, BTN_H, "Day >", C_PANEL2, ACT_NEXT_DAY);
        } else if (g_page == PAGE_PROFILE) {
            add_button(x0, r1, w3, BTN_H, "A  Change", C_GRAD, ACT_EDIT_ROW);
            add_button(x1, r1, w3, BTN_H, "Y  Log weight", C_GRAD, ACT_LOG_WEIGHT);
        } else if (g_page == PAGE_REPORT) {
            add_button(x0, r1, w3, BTN_H, "Y  < Day", C_PANEL2, ACT_PREV_DAY);
            add_button(x1, r1, w3, BTN_H, "B  Today", C_PANEL2, ACT_TODAY);
            add_button(x2, r1, w3, BTN_H, "X  Day >", C_PANEL2, ACT_NEXT_DAY);
        } else {
            add_button(x0, r1, w3, BTN_H, "Y  < Week", C_PANEL2, ACT_PREV_WEEK);
            add_button(x1, r1, w3, BTN_H, "B  This week", C_PANEL2, ACT_TODAY);
            add_button(x2, r1, w3, BTN_H, "X  Week >", C_PANEL2, ACT_NEXT_WEEK);
        }
        break;
    }
    default:
        break;
    }
}

/* What sits in the row above the tab bar. */
static bool slot_row_visible(void)
{
    return !g_building && !g_scanning && !g_unit_picking && !g_show_micros && g_tab != TAB_PROFILE &&
           !(g_tab == TAB_BRANDS && g_brand_open < 0);
}

static bool build_strip_visible(void)
{
    return g_building && !g_scanning && g_tab != TAB_PROFILE;
}

static void handle_touch(void)
{
    touchPosition tp;
    hidTouchRead(&tp);
    float x = tp.px, y = tp.py;

    if (y >= TAB_Y && !g_scanning && !g_unit_picking) {
        int t = (int)(x / (BOT_W / TAB_COUNT));
        if (t >= 0 && t < TAB_COUNT)
            switch_tab((Tab)t);
        return;
    }
    if (y >= SLOT_Y - 2 && y < SLOT_Y + 18) {
        if (slot_row_visible()) {
            if (x < 60) do_action(ACT_MEAL_PREV);
            else if (x > BOT_W - 60) do_action(ACT_MEAL_NEXT);
            return;
        }
        if (build_strip_visible()) {
            do_action(ACT_GO_MEALS);
            return;
        }
    }
    for (int i = 0; i < g_button_count; i++) {
        Button *b = &g_buttons[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            do_action(b->action);
            return;
        }
    }
}

/* ---- top screen --------------------------------------------------------- */
static void header(const char *title, const char *right)
{
    grad_rect(0, 0, TOP_W, 22, 0, 1);
    text(10, 3, 0.55f, C_INK, 0, "%s", title);
    if (right)
        text(TOP_W - 10, 4, 0.45f, C_INK, C2D_AlignRight, "%s", right);
}

static void fav_dot(float x, float y)
{
    C2D_DrawCircleSolid(x, y, 0.5f, 3.5f, C_MINT);
}

/* One list row: name (and brand) on the left, a value on the right. */
static void food_row(float y, bool selected, const char *name, const char *brand, bool fav,
                     const char *right, u32 accent)
{
    if (selected) {
        rrect(4, y - 1, TOP_W - 8, ROW_H, 5, C_PANEL2);
        C2D_DrawRectSolid(4, y - 1, 0.45f, 3, ROW_H, accent);
    }
    if (fav)
        fav_dot(13, y + 7);
    /* keep the name clear of the value on the right */
    if (brand && brand[0])
        text_fit(20, y, TEXT_SCALE, TOP_W - 145, C_TEXT, 0, "%.28s  (%.14s)", name, brand);
    else
        text_fit(20, y, TEXT_SCALE, TOP_W - 145, C_TEXT, 0, "%.40s", name);
    text(TOP_W - 10, y, TEXT_SCALE, C_MUTED, C2D_AlignRight, "%s", right);
}

static void scroll_hint(float y, int scroll, int rows, int count)
{
    if (count > rows)
        text(TOP_W - 10, y, 0.4f, C_MUTED, C2D_AlignRight, "%d-%d of %d", scroll + 1,
             scroll + rows < count ? scroll + rows : count, count);
}

static u32 meal_color(Meal m)
{
    return grad((float)m / (MEAL_COUNT - 1));
}

/* ---- the macaroni mascot ---------------------------------------------- */
static void draw_mascot(float x, float base_y, bool cheer, bool party)
{
    float t = g_anim / 60.0f;
    float bounce = cheer ? fabsf(sinf(t * 7.0f)) * 6.0f : fabsf(sinf(t * 2.2f)) * 2.0f;
    float y = base_y - bounce;
    u32 outline = C2D_Color32(0xc2, 0x74, 0x0c, 0xff);
    u32 body = C2D_Color32(0xff, 0xc9, 0x47, 0xff);
    u32 shine = C2D_Color32(0xff, 0xe9, 0xa8, 0xff);
    u32 hole = C2D_Color32(0x9a, 0x55, 0x08, 0xff);
    u32 ink = C2D_Color32(0x3b, 0x1f, 0x0a, 0xff);
    u32 cheek = C2D_Color32(0xff, 0x7a, 0x9a, 0xb0);
    const float R = 15, TUBE = 9.5f, DEG = 3.14159265f / 180.0f;

    /* shadow on the "floor" shrinks while jumping */
    float sw = 30 - bounce * 1.5f;
    C2D_DrawEllipseSolid(x - sw / 2, base_y + 16, 0.42f, sw, 5, C2D_Color32(0, 0, 0, 0x50));

    /* the elbow: an arch of circles, outline first */
    for (int a = 185; a <= 355; a += 5)
        C2D_DrawCircleSolid(x + R * cosf(a * DEG), y + R * sinf(a * DEG), 0.43f, TUBE + 1.6f, outline);
    for (int a = 185; a <= 355; a += 5)
        C2D_DrawCircleSolid(x + R * cosf(a * DEG), y + R * sinf(a * DEG), 0.44f, TUBE, body);
    /* ridges */
    for (int a = 205; a <= 335; a += 26)
        C2D_DrawLine(x + (R - TUBE + 2) * cosf(a * DEG), y + (R - TUBE + 2) * sinf(a * DEG), outline,
                     x + (R + TUBE - 2) * cosf(a * DEG), y + (R + TUBE - 2) * sinf(a * DEG), outline, 1.0f,
                     0.45f);
    for (int a = 215; a <= 255; a += 5)
        C2D_DrawCircleSolid(x + (R + 4.5f) * cosf(a * DEG), y + (R + 4.5f) * sinf(a * DEG), 0.46f, 2.0f, shine);
    /* the open ends */
    C2D_DrawEllipseSolid(x - R - TUBE + 1, y + 2, 0.46f, 2 * TUBE - 2, 7, outline);
    C2D_DrawEllipseSolid(x - R - TUBE + 3, y + 3.5f, 0.47f, 2 * TUBE - 6, 4, hole);
    C2D_DrawEllipseSolid(x + R - TUBE + 1, y + 2, 0.46f, 2 * TUBE - 2, 7, outline);
    C2D_DrawEllipseSolid(x + R - TUBE + 3, y + 3.5f, 0.47f, 2 * TUBE - 6, 4, hole);

    /* face on top of the arch */
    float fy = y - R + 1;
    bool blink = fmodf(t, 4.0f) < 0.12f;
    for (int side = -1; side <= 1; side += 2) {
        float ex = x + side * 5.5f;
        if (blink) {
            C2D_DrawLine(ex - 2, fy - 2, ink, ex + 2, fy - 2, ink, 1.2f, 0.48f);
        } else if (cheer) { /* happy closed eyes: ^ ^ */
            C2D_DrawLine(ex - 2.2f, fy - 1, ink, ex, fy - 3.5f, ink, 1.2f, 0.48f);
            C2D_DrawLine(ex, fy - 3.5f, ink, ex + 2.2f, fy - 1, ink, 1.2f, 0.48f);
        } else {
            C2D_DrawCircleSolid(ex, fy - 2, 0.48f, 2.1f, ink);
            C2D_DrawCircleSolid(ex + 0.7f, fy - 2.8f, 0.49f, 0.8f, C_TEXT);
        }
        C2D_DrawCircleSolid(x + side * 9.5f, fy + 2, 0.48f, 2.0f, cheek);
    }
    if (cheer) {
        C2D_DrawEllipseSolid(x - 3, fy + 1, 0.48f, 6, 5, ink);
        C2D_DrawEllipseSolid(x - 1.5f, fy + 3.5f, 0.49f, 3, 2, C_WARN);
    } else {
        for (int a = 30; a <= 150; a += 15)
            C2D_DrawCircleSolid(x + 3 * cosf(a * DEG), fy + 0.5f + 2.5f * sinf(a * DEG), 0.48f, 0.7f, ink);
    }

    /* little arms: waving when cheering */
    float wave = cheer ? sinf(t * 14.0f) * 3 : 0;
    float ay = cheer ? -14 : 6;
    C2D_DrawLine(x - R - TUBE + 2, y - 2, outline, x - R - TUBE - 6, y + ay + wave, outline, 2.0f, 0.42f);
    C2D_DrawLine(x + R + TUBE - 2, y - 2, outline, x + R + TUBE + 6, y + ay - wave, outline, 2.0f, 0.42f);
    C2D_DrawCircleSolid(x - R - TUBE - 6, y + ay + wave, 0.43f, 2.2f, body);
    C2D_DrawCircleSolid(x + R + TUBE + 6, y + ay - wave, 0.43f, 2.2f, body);

    if (cheer) { /* sparkles */
        for (int i = 0; i < 3; i++) {
            float a = t * 2.5f + i * 2.1f;
            float sx = x + cosf(a) * 30, sy = y - 8 + sinf(a) * 14;
            C2D_DrawLine(sx - 2.5f, sy, C_MINT, sx + 2.5f, sy, C_MINT, 1.0f, 0.5f);
            C2D_DrawLine(sx, sy - 2.5f, C_MINT, sx, sy + 2.5f, C_MINT, 1.0f, 0.5f);
        }
    }

    if (party) { /* a striped party hat for a 3+ day streak */
        float hx = x + 1, hy = y - R - TUBE + 2;
        u32 pink = C2D_Color32(0xf4, 0x72, 0xb6, 0xff), sky = C2D_Color32(0x60, 0xa5, 0xfa, 0xff);
        C2D_DrawTriangle(hx - 7, hy, pink, hx + 7, hy, sky, hx + 3, hy - 17, pink, 0.5f);
        C2D_DrawLine(hx - 4.5f, hy - 5, C_TEXT, hx + 5.5f, hy - 5, C_TEXT, 1.2f, 0.51f);
        C2D_DrawLine(hx - 1.5f, hy - 11, C_TEXT, hx + 4.5f, hy - 11, C_TEXT, 1.2f, 0.51f);
        C2D_DrawCircleSolid(hx + 3, hy - 17, 0.52f, 2.5f, C2D_Color32(0xfd, 0xe0, 0x47, 0xff));
    }
}

static void water_drop(float x, float y, u32 color)
{
    C2D_DrawCircleSolid(x, y + 2, 0.5f, 3.5f, color);
    C2D_DrawTriangle(x - 3.3f, y + 1, color, x + 3.3f, y + 1, color, x, y - 5, color, 0.5f);
}

static void flame(float x, float y, bool lit)
{
    u32 outer = lit ? C2D_Color32(0xfb, 0x92, 0x3c, 0xff) : C_BAR_BG;
    u32 inner = lit ? C2D_Color32(0xfd, 0xe0, 0x47, 0xff) : C_PANEL2;
    C2D_DrawCircleSolid(x, y + 2, 0.5f, 4.0f, outer);
    C2D_DrawTriangle(x - 3.8f, y + 1, outer, x + 3.8f, y + 1, outer, x + 1, y - 7, outer, 0.5f);
    C2D_DrawCircleSolid(x, y + 3, 0.51f, 2.0f, inner);
}

/* Progress toward a micronutrient goal. Going past a goal (fiber, iron...)
 * is fine and stays green; only limits (sugar, sodium...) turn red. */
static void micro_bar(float x, float y, float w, float h, Micro m, float value, float target)
{
    if (micro_info(m)->limit) {
        bar(x, y, w, h, value, target, C_TURQ);
        return;
    }
    rrect(x, y, w, h, h / 2, C_BAR_BG);
    if (target <= 0 || value <= 0)
        return;
    float frac = value / target;
    if (frac > 1)
        frac = 1;
    float fw = w * frac < h ? h : w * frac;
    rrect(x, y, fw, h, h / 2, frac >= 1 ? C_GREEN : C_TURQ);
}

static void micro_value(char *out, size_t size, float v)
{
    if (v >= 100 || v == 0)
        snprintf(out, size, "%.0f", v);
    else if (v >= 10)
        snprintf(out, size, "%.0f", v);
    else
        snprintf(out, size, "%.1f", v);
}

static void draw_top_today(void)
{
    Totals t = sum_entries(g_day.entries, g_day.count, -1);
    bool is_today = strcmp(g_day.date, g_today) == 0;
    bool female = g_profile.sex == SEX_FEMALE;
    const float L = 322; /* right edge of the numbers; the mascot lives right of it */
    C2D_DrawRectSolid(0, 0, 0.4f, TOP_W, 98, C_PANEL);
    grad_rect(0, 96, TOP_W, 2, 0, 1);
    text(10, 5, 0.55f, C_TEXT, 0, "%s%s", g_day.date, is_today ? "  (today)" : "");
    text(L, 5, 0.55f, t.kcal > g_targets.kcal ? C_WARN : C_TURQ, C2D_AlignRight, "%.0f / %.0f kcal", t.kcal,
         g_targets.kcal);
    bar(10, 24, L - 10, 8, t.kcal, g_targets.kcal, C_GRAD);

    float col = (L - 10) / 3.0f;
    text(10, 35, 0.42f, C_TEXT, 0, "Protein %.0f/%.0f g", t.protein, g_targets.protein);
    text(10 + col, 35, 0.42f, C_TEXT, 0, "Carbs %.0f/%.0f g", t.carbs, g_targets.carbs);
    text(10 + 2 * col, 35, 0.42f, C_TEXT, 0, "Fat %.0f/%.0f g", t.fat, g_targets.fat);
    bar(10, 49, col - 8, 5, t.protein, g_targets.protein, C_PROTEIN);
    bar(10 + col, 49, col - 8, 5, t.carbs, g_targets.carbs, C_CARBS);
    bar(10 + 2 * col, 49, col - 8, 5, t.fat, g_targets.fat, C_FAT);

    /* key micronutrients, a bit smaller */
    float mc = (L - 10) / MICRO_KEY_COUNT;
    for (int i = 0; i < MICRO_KEY_COUNT; i++) {
        const MicroInfo *mi = micro_info((Micro)i);
        float target = micro_target((Micro)i, female, g_profile.age);
        float x = 10 + i * mc;
        char v[16], tg[16];
        micro_value(v, sizeof v, t.micro[i]);
        micro_value(tg, sizeof tg, target);
        text(x, 59, 0.36f, C_MUTED, 0, "%s%s", mi->name, mi->limit ? " (max)" : "");
        text(x, 70, 0.36f, C_TEXT, 0, "%s/%s %s", v, tg, mi->unit);
        micro_bar(x, 83, mc - 8, 3, (Micro)i, t.micro[i], target);
    }

    /* mascot, water and streak on the right */
    int streak = streak_now();
    bool protein_hit = g_targets.protein > 0 && t.protein >= g_targets.protein;
    if (g_cheer > 0)
        g_cheer--;
    draw_mascot(364, 36, g_cheer > 0 || protein_hit, streak >= 3);
    int glasses = water_glasses();
    water_drop(338, 68, glasses >= water_goal() ? C_BLUE : C_TURQ);
    text(347, 62, 0.4f, C_TEXT, 0, "%d/%d", glasses, water_goal());
    flame(338, 84, streak > 0);
    text(347, 78, 0.4f, streak > 0 ? C_TEXT : C_MUTED, 0, "%d day%s", streak, streak == 1 ? "" : "s");

    float y = 102;
    if (g_day.count == 0) {
        rrect(40, y + 24, TOP_W - 80, 60, 12, C_PANEL);
        text(TOP_W / 2, y + 34, 0.6f, C_TURQ, C2D_AlignCenter, "Nothing logged yet!");
        text(TOP_W / 2, y + 56, 0.45f, C_MUTED, C2D_AlignCenter, "A to search, Scan a barcode, or Quick add");
        return;
    }
    int rows = visible_rows(TAB_TODAY);
    for (int row = 0; row < rows && g_scroll[TAB_TODAY] + row < (int)g_day.count; row++) {
        int idx = g_scroll[TAB_TODAY] + row;
        const Entry *e = &g_day.entries[g_order[idx]];
        Totals et = entry_totals(e);
        char right[40];
        if (e->quick)
            snprintf(right, sizeof right, "quick  %.0f kcal", et.kcal);
        else {
            char amt[32];
            amount_text(e, amt, sizeof amt);
            snprintf(right, sizeof right, "%s  %.0f kcal", amt, et.kcal);
        }
        float ry = y + row * ROW_H;
        food_row(ry, idx == g_sel[TAB_TODAY], e->food.name, e->quick ? "" : e->food.brand,
                 !e->quick && is_fav(&e->food), right, meal_color(e->meal));
        /* meal color tag on the far left */
        C2D_DrawRectSolid(4, ry + 2, 0.5f, 3, ROW_H - 5, meal_color(e->meal));
    }
    scroll_hint(y + rows * ROW_H + 1, g_scroll[TAB_TODAY], rows, (int)g_day.count);
}

static void draw_top_micros(void)
{
    Totals t = sum_entries(g_day.entries, g_day.count, -1);
    bool female = g_profile.sex == SEX_FEMALE;
    char title[48];
    snprintf(title, sizeof title, "All micronutrients  -  %s", g_day.date);
    header(title, "< >");
    for (int i = 0; i < MICRO_COUNT; i++) {
        const MicroInfo *mi = micro_info((Micro)i);
        float target = micro_target((Micro)i, female, g_profile.age);
        float y = 27 + i * 14.6f;
        if (i % 2 == 0)
            rrect(4, y - 1, TOP_W - 8, 14, 4, C_PANEL);
        text(12, y, 0.43f, i < MICRO_KEY_COUNT ? C_TEXT : C_MUTED, 0, "%s", mi->name);
        micro_bar(118, y + 4, 150, 5, (Micro)i, t.micro[i], target);
        char v[16], tg[16];
        micro_value(v, sizeof v, t.micro[i]);
        micro_value(tg, sizeof tg, target);
        text(TOP_W - 12, y, 0.43f, C_TEXT, C2D_AlignRight, "%s / %s%s %s", v, mi->limit ? "max " : "", tg,
             mi->unit);
    }
}

static void draw_unit_picker(void)
{
    char title[80];
    snprintf(title, sizeof title, "How much %.40s?", g_unit_food.name);
    header(title, NULL);
    for (int i = 0; i < g_unit_n; i++) {
        float y = 34 + i * 22;
        bool on = i == g_unit_sel;
        if (on) {
            rrect(4, y - 3, TOP_W - 8, 20, 6, C_PANEL2);
            C2D_DrawRectSolid(4, y - 3, 0.45f, 3, 20, grad((float)i / (UNIT_COUNT - 1)));
        }
        char label[64];
        unit_choice_text(&g_unit_food, g_unit_opts[i], label, sizeof label);
        text(16, y, 0.52f, on ? C_TEXT : C_MUTED, 0, "%s", label);
        Entry e;
        memset(&e, 0, sizeof e);
        e.food = g_unit_food;
        e.grams = unit_grams(&g_unit_food, g_unit_opts[i]);
        if (g_unit_opts[i] == UNIT_GRAMS)
            e.grams = 100;
        text(TOP_W - 12, y + 1, 0.45f, C_MUTED, C2D_AlignRight, "%s%.0f kcal",
             g_unit_opts[i] == UNIT_GRAMS ? "100 g: " : "1: ", entry_totals(&e).kcal);
    }
    if (g_unit_food.serving_g <= 0)
        text(TOP_W / 2, 34 + g_unit_n * 22 + 8, 0.42f, C_MUTED, C2D_AlignCenter,
             "This product doesn't list a serving size, so servings are off");
}

static void draw_top_scanner(void)
{
    C2D_DrawImageAt(scanner_image(), 0, 0, 0.3f, NULL, 1.0f, 1.0f);
    /* frame the scan area and draw the guide line */
    u32 dim = C2D_Color32(0, 0, 0, 0x70);
    C2D_DrawRectSolid(0, 0, 0.4f, TOP_W, 70, dim);
    C2D_DrawRectSolid(0, 170, 0.4f, TOP_W, 70, dim);
    float pulse = 0.5f + 0.5f * sinf(g_anim / 8.0f);
    C2D_DrawRectSolid(30, 119, 0.5f, TOP_W - 60, 2, C2D_Color32(0xfb, 0x71, 0x85, (u8)(120 + 120 * pulse)));
    u32 c = C_TURQ;
    float x0 = 30, x1 = TOP_W - 30, y0 = 74, y1 = 166, k = 14;
    C2D_DrawRectSolid(x0, y0, 0.5f, k, 3, c);
    C2D_DrawRectSolid(x0, y0, 0.5f, 3, k, c);
    C2D_DrawRectSolid(x1 - k, y0, 0.5f, k, 3, c);
    C2D_DrawRectSolid(x1 - 3, y0, 0.5f, 3, k, c);
    C2D_DrawRectSolid(x0, y1 - 3, 0.5f, k, 3, c);
    C2D_DrawRectSolid(x0, y1 - k, 0.5f, 3, k, c);
    C2D_DrawRectSolid(x1 - k, y1 - 3, 0.5f, k, 3, c);
    C2D_DrawRectSolid(x1 - 3, y1 - k, 0.5f, 3, k, c);
    text(TOP_W / 2, 24, 0.55f, C_TEXT, C2D_AlignCenter, "Line the barcode up with the red line");
    text(TOP_W / 2, 196, 0.45f, C_MINT, C2D_AlignCenter, "Hold it flat and still, a hand's length away");
}

static void draw_food_list(const Food *foods, int count, int sel, int scroll, float y0, u32 accent)
{
    int rows = visible_rows(g_tab);
    for (int row = 0; row < rows && scroll + row < count; row++) {
        int idx = scroll + row;
        char right[24];
        snprintf(right, sizeof right, "%.0f kcal/100g", foods[idx].kcal);
        food_row(y0 + row * ROW_H, idx == sel, foods[idx].name, foods[idx].brand, is_fav(&foods[idx]),
                 right, accent);
    }
    scroll_hint(y0 + rows * ROW_H + 1, scroll, rows, count);
}

static void empty_card(const char *title, const char *hint)
{
    rrect(40, 80, TOP_W - 80, 64, 12, C_PANEL);
    grad_rect(52, 80, TOP_W - 104, 3, 0, 1);
    text(TOP_W / 2, 92, 0.6f, C_TURQ, C2D_AlignCenter, "%s", title);
    text(TOP_W / 2, 116, 0.45f, C_MUTED, C2D_AlignCenter, "%s", hint);
}

/* Header text while building a meal, so it shows on every tab. */
static const char *building_note(void)
{
    static char note[48];
    snprintf(note, sizeof note, "Meal: %.16s (%d)", g_draft.name, (int)g_draft.count);
    return g_building ? note : NULL;
}

static void draw_top_search(void)
{
    char title[96];
    if (g_have_results)
        snprintf(title, sizeof title, "Search: %.28s", g_query);
    else
        snprintf(title, sizeof title, "Search  -  recent foods");
    header(title, building_note());
    if (g_pending == PEND_SEARCH) {
        empty_card("Searching...", "Asking Open Food Facts");
        return;
    }
    int count = list_count(TAB_SEARCH);
    if (count == 0) {
        empty_card(g_have_results ? "No results" : "Find a food", "Press Y to search online");
        return;
    }
    draw_food_list(g_have_results ? g_results : g_cache.items, count, g_sel[TAB_SEARCH],
                   g_scroll[TAB_SEARCH], 28, C_GREEN);
}

static void draw_top_brands(void)
{
    if (g_brand_open >= 0) {
        char title[64];
        snprintf(title, sizeof title, "Brands  >  %.24s", g_brands.names[g_brand_open]);
        header(title, building_note());
        if (g_pending == PEND_BRAND) {
            empty_card("Loading products...", "Asking Open Food Facts");
            return;
        }
        if (g_brand_count == 0) {
            empty_card("No products yet", "Press Y to refresh");
            return;
        }
        draw_food_list(g_brand_foods, g_brand_count, g_brand_sel, g_brand_scroll, 28, C_TURQ);
        return;
    }

    header("My Brands", building_note());
    if (g_brands.count == 0) {
        empty_card("Follow your favorite brands", "Press Y to add one, like Quest or Barebells");
        return;
    }
    float tw = (TOP_W - 16 - 2 * 8) / 3, th = 30;
    int sel = g_sel[TAB_BRANDS];
    /* scroll by rows of 3 so the selected tile stays on screen (6 rows fit) */
    int first_row = sel / 3 >= 6 ? sel / 3 - 5 : 0;
    for (int i = first_row * 3; i < (int)g_brands.count && i < (first_row + 6) * 3; i++) {
        int r = i / 3 - first_row, c = i % 3;
        float x = 8 + c * (tw + 8), y = 30 + r * (th + 4);
        if (i == sel)
            rrect(x - 2, y - 2, tw + 4, th + 4, 10, C_TEXT);
        /* the tiles in a row form one continuous gradient */
        rrect_grad(x, y, tw, th, 9, x / TOP_W, (x + tw) / TOP_W);
        text(x + tw / 2, y + 7, 0.55f, C_INK, C2D_AlignCenter, "%.16s", g_brands.names[i]);
    }
}

static void draw_top_favs(void)
{
    header("Favorites", building_note());
    if (g_favs.count == 0) {
        empty_card("No favorites yet", "Press X on any food to make it a favorite");
        return;
    }
    int rows = visible_rows(TAB_FAVS);
    for (int row = 0; row < rows && g_scroll[TAB_FAVS] + row < (int)g_favs.count; row++) {
        int idx = g_scroll[TAB_FAVS] + row;
        const Favorite *f = &g_favs.items[idx];
        Entry e;
        memset(&e, 0, sizeof e);
        e.food = f->food;
        e.grams = f->grams;
        char right[32];
        snprintf(right, sizeof right, "%.0f g  %.0f kcal", f->grams, entry_totals(&e).kcal);
        food_row(28 + row * ROW_H, idx == g_sel[TAB_FAVS], f->food.name, f->food.brand, true, right,
                 C_TURQ);
    }
    scroll_hint(28 + rows * ROW_H + 1, g_scroll[TAB_FAVS], rows, (int)g_favs.count);
}

static Totals meal_totals(const SavedMeal *m)
{
    return sum_entries(m->items, m->count, -1);
}

static void entry_right(const Entry *e, char *out, size_t size)
{
    if (e->quick)
        snprintf(out, size, "quick  %.0f kcal", entry_totals(e).kcal);
    else {
        char amt[32];
        amount_text(e, amt, sizeof amt);
        snprintf(out, size, "%s  %.0f kcal", amt, entry_totals(e).kcal);
    }
}

static void draw_top_meals(void)
{
    if (g_building) {
        char title[96], right[32];
        Totals t = meal_totals(&g_draft);
        snprintf(title, sizeof title, "New meal: %.30s", g_draft.name);
        snprintf(right, sizeof right, "%.0f kcal", t.kcal);
        header(title, right);
        text(10, 26, 0.42f, C_MUTED, 0, "P %.0f g   C %.0f g   F %.0f g   -   %d foods", t.protein, t.carbs,
             t.fat, (int)g_draft.count);
        if (g_draft.count == 0) {
            empty_card("Add foods to your meal", "Y opens Search. Favs and Brands work too");
            return;
        }
        int rows = visible_rows(TAB_MEALS);
        for (int row = 0; row < rows && g_scroll[TAB_MEALS] + row < (int)g_draft.count; row++) {
            int idx = g_scroll[TAB_MEALS] + row;
            const Entry *e = &g_draft.items[idx];
            char r[40];
            entry_right(e, r, sizeof r);
            food_row(42 + row * ROW_H, idx == g_sel[TAB_MEALS], e->food.name, e->quick ? "" : e->food.brand,
                     false, r, C_GREEN);
        }
        scroll_hint(42 + rows * ROW_H + 1, g_scroll[TAB_MEALS], rows, (int)g_draft.count);
        return;
    }

    header("Saved meals", NULL);
    if (g_meals.count == 0) {
        empty_card("No saved meals yet", "Tap + New meal, then pick foods like in Search");
        return;
    }
    int rows = visible_rows(TAB_MEALS);
    for (int row = 0; row < rows && g_scroll[TAB_MEALS] + row < (int)g_meals.count; row++) {
        int idx = g_scroll[TAB_MEALS] + row;
        const SavedMeal *m = &g_meals.items[idx];
        char right[32], items[16];
        snprintf(right, sizeof right, "%.0f kcal", meal_totals(m).kcal);
        snprintf(items, sizeof items, "%d foods", (int)m->count);
        food_row(28 + row * ROW_H, idx == g_sel[TAB_MEALS], m->name, items, false, right, C_GREEN);
    }
    scroll_hint(28 + rows * ROW_H + 1, g_scroll[TAB_MEALS], rows, (int)g_meals.count);
}

/* ---- profile, report, charts ------------------------------------------- */
static const char *ROW_LABELS[PR_COUNT] = {"Age",      "Sex",        "Weight", "Height",   "BMI",
                                           "Body fat", "Activity",   "Water",  "Sounds",   "Calories",
                                           "Protein",  "Carbs",      "Fat"};

static float profile_row_y(int row)
{
    return 27 + row * 15 + (row >= PR_KCAL ? 14 : 0);
}

static void profile_value(ProfileRow row, char *out, size_t size)
{
    const UserProfile *p = &g_profile;
    float kg = current_weight();
    switch (row) {
    case PR_AGE:
        if (p->age > 0) snprintf(out, size, "%d years", p->age);
        else snprintf(out, size, "not set");
        break;
    case PR_SEX: snprintf(out, size, "%s", p->sex == SEX_MALE ? "Male" : "Female"); break;
    case PR_WEIGHT:
        if (kg > 0) snprintf(out, size, "%.1f kg", kg);
        else snprintf(out, size, "not logged yet");
        break;
    case PR_HEIGHT:
        if (p->height_cm > 0) snprintf(out, size, "%.0f cm", p->height_cm);
        else snprintf(out, size, "not set");
        break;
    case PR_BMI: {
        float b = bmi(kg, p->height_cm);
        if (b > 0) snprintf(out, size, "%.1f", b);
        else snprintf(out, size, "needs weight and height");
        break;
    }
    case PR_BODY_FAT:
        if (p->body_fat > 0) snprintf(out, size, "%.0f %%", p->body_fat);
        else snprintf(out, size, "not set");
        break;
    case PR_ACTIVITY: snprintf(out, size, "%s", activity_name(p->activity)); break;
    case PR_WATER: snprintf(out, size, "%d glasses a day", water_goal()); break;
    case PR_SOUNDS: snprintf(out, size, "%s", p->sounds_off ? "Off" : "On"); break;
    case PR_KCAL: snprintf(out, size, "%.0f kcal", g_targets.kcal); break;
    case PR_PROTEIN: snprintf(out, size, "%.0f g", g_targets.protein); break;
    case PR_CARBS: snprintf(out, size, "%.0f g", g_targets.carbs); break;
    case PR_FAT: snprintf(out, size, "%.0f g", g_targets.fat); break;
    default: out[0] = '\0'; break;
    }
}

static void draw_page_profile(void)
{
    header("Profile", "< Report  Charts >");
    text(142, profile_row_y(PR_KCAL) - 12, 0.38f, C_MUTED, 0, "Daily targets");
    for (int row = 0; row < PR_COUNT; row++) {
        float y = profile_row_y(row);
        bool on = row == g_sel[TAB_PROFILE];
        if (on) {
            rrect(4, y - 1, TOP_W - 8, ROW_H, 5, C_PANEL2);
            C2D_DrawRectSolid(4, y - 1, 0.45f, 3, ROW_H, grad((float)row / (PR_COUNT - 1)));
        }
        char value[48];
        profile_value((ProfileRow)row, value, sizeof value);
        text(16, y, TEXT_SCALE, C_MUTED, 0, "%s", ROW_LABELS[row]);
        text(TOP_W - 12, y, TEXT_SCALE, row == PR_BMI ? C_MUTED : C_TEXT, C2D_AlignRight, "%s", value);
    }
    grad_rect(16, profile_row_y(PR_KCAL) - 4, 120, 2, 0, 1);
}

static void draw_page_report(void)
{
    char title[48];
    snprintf(title, sizeof title, "Report  -  %s", g_day.date);
    header(title, "< >");
    float kg = weight_for_day();
    float rest = bmr(&g_profile, kg);
    if (rest <= 0) {
        empty_card("Fill in your profile first", "Age, height and a weight are needed");
        return;
    }
    Totals t = sum_entries(g_day.entries, g_day.count, -1);
    float basic = rest * (activity_factor(g_profile.activity) - 1.0f);
    float steps = steps_kcal(g_step_total, kg, g_profile.height_cm);
    float workouts = workouts_total(&g_workouts);
    float burned = rest + basic + workouts;

    /* two big numbers */
    float cw = (TOP_W - 30) / 2;
    rrect(10, 30, cw, 50, 10, C_PANEL);
    rrect(20 + cw, 30, cw, 50, 10, C_PANEL);
    grad_rect(10 + 10, 30, cw - 20, 3, 0, 0.5f);
    grad_rect(20 + cw + 10, 30, cw - 20, 3, 0.5f, 1);
    text(10 + cw / 2, 37, 0.45f, C_MUTED, C2D_AlignCenter, "Eaten");
    text(10 + cw / 2, 52, 0.75f, C_GREEN, C2D_AlignCenter, "%.0f kcal", t.kcal);
    text(20 + cw * 1.5f, 37, 0.45f, C_MUTED, C2D_AlignCenter, "Burned");
    text(20 + cw * 1.5f, 52, 0.75f, C_BLUE, C2D_AlignCenter, "%.0f kcal", burned);

    /* side by side bars on one scale */
    float top = t.kcal > burned ? t.kcal : burned;
    if (top <= 0) top = 1;
    text(10, 88, 0.42f, C_MUTED, 0, "Eaten");
    bar(70, 91, TOP_W - 80, 8, t.kcal, top, C_GREEN);
    text(10, 102, 0.42f, C_MUTED, 0, "Burned");
    bar(70, 105, TOP_W - 80, 8, burned, top, C_BLUE);

    /* what makes up "burned" */
    float y = 122;
    text(10, y, 0.48f, C_TEXT, 0, "Burned");
    y += 16;
    text(20, y, 0.45f, C_MUTED, 0, "Resting (BMR)");
    text(TOP_W - 12, y, 0.45f, C_TEXT, C2D_AlignRight, "%.0f kcal", rest);
    y += 14;
    text(20, y, 0.45f, C_MUTED, 0, "Basic activity (%s)", activity_name(g_profile.activity));
    text(TOP_W - 12, y, 0.45f, C_TEXT, C2D_AlignRight, "%.0f kcal", basic);
    y += 14;
    text(20, y, 0.45f, C_MUTED, 0, "Workouts (%d)", (int)g_workouts.count);
    text(TOP_W - 12, y, 0.45f, C_TEXT, C2D_AlignRight, "%.0f kcal", workouts);
    if (g_step_total > 0)
        text(TOP_W - 12, 122, 0.4f, C_TURQ, C2D_AlignRight, "Heads up: %u steps = about %.0f kcal", g_step_total,
             steps);

    /* where the eaten energy came from */
    y += 20;
    float pk = t.protein * 4, ck = t.carbs * 4, fk = t.fat * 9, sum = pk + ck + fk;
    text(10, y, 0.48f, C_TEXT, 0, "Eaten");
    if (sum > 0) {
        y += 17;
        float w = TOP_W - 20, x = 10;
        float parts[3] = {pk, ck, fk};
        u32 cols[3] = {C_PROTEIN, C_CARBS, C_FAT};
        for (int i = 0; i < 3; i++) {
            float pw = w * parts[i] / sum;
            if (pw > 0) C2D_DrawRectSolid(x, y, 0.4f, pw, 8, cols[i]);
            x += pw;
        }
        y += 11;
        text(10, y, 0.42f, C_MUTED, 0, "Protein %.0f%%   Carbs %.0f%%   Fat %.0f%%", pk / sum * 100,
             ck / sum * 100, fk / sum * 100);
    } else {
        text(TOP_W - 12, y, 0.45f, C_MUTED, C2D_AlignRight, "nothing logged");
    }
}

static void day_number(const char *date, char *out, size_t size)
{
    /* "YYYY-MM-DD" -> "DD" */
    snprintf(out, size, "%.2s", strlen(date) >= 10 ? date + 8 : "?");
}

static void draw_page_charts(void)
{
    if (g_week_dirty)
        compute_week();
    char title[48];
    snprintf(title, sizeof title, "Charts  -  week to %s", g_day.date);
    header(title, "< >");

    /* calories per day, split by macro */
    text(10, 25, 0.45f, C_TEXT, 0, "Calories");
    float lx = TOP_W - 150;
    const char *names[3] = {"Protein", "Carbs", "Fat"};
    u32 cols[3] = {C_PROTEIN, C_CARBS, C_FAT};
    for (int i = 0; i < 3; i++) {
        C2D_DrawRectSolid(lx + i * 50, 30, 0.4f, 7, 7, cols[i]);
        text(lx + i * 50 + 10, 26, 0.4f, C_MUTED, 0, "%s", names[i]);
    }
    float x0 = 36, y_top = 44, y_base = 118, w = TOP_W - x0 - 10;
    float scale = g_targets.kcal;
    for (int i = 0; i < WEEK; i++)
        if (g_week[i].kcal > scale) scale = g_week[i].kcal;
    scale *= 1.1f;
    if (scale <= 0) scale = 1;
    C2D_DrawRectSolid(x0, y_base, 0.4f, w, 1, C_BAR_BG);
    /* target line */
    float ty = y_base - (y_base - y_top) * g_targets.kcal / scale;
    for (float x = x0; x < x0 + w; x += 8)
        C2D_DrawRectSolid(x, ty, 0.45f, 4, 1, C_MUTED);
    text(x0 - 4, ty - 6, 0.35f, C_MUTED, C2D_AlignRight, "%.0f", g_targets.kcal);
    float slot = w / WEEK, bw = slot * 0.55f;
    for (int i = 0; i < WEEK; i++) {
        const Totals *d = &g_week[i];
        float bx = x0 + i * slot + (slot - bw) / 2;
        float h = (y_base - y_top) * d->kcal / scale;
        float pk = d->protein * 4, ck = d->carbs * 4, fk = d->fat * 9, sum = pk + ck + fk;
        if (h > 0 && sum > 0) {
            float parts[3] = {pk, ck, fk};
            float y = y_base;
            for (int k = 0; k < 3; k++) {
                float ph = h * parts[k] / sum;
                C2D_DrawRectSolid(bx, y - ph, 0.4f, bw, ph, cols[k]);
                y -= ph;
            }
        } else if (h > 0) {
            C2D_DrawRectSolid(bx, y_base - h, 0.4f, bw, h, C_TURQ);
        }
        if (d->kcal > 0)
            text(bx + bw / 2, y_base - h - 10, 0.35f, C_TEXT, C2D_AlignCenter, "%.0f", d->kcal);
        char dn[4];
        day_number(g_week_dates[i], dn, sizeof dn);
        bool cur = i == WEEK - 1;
        text(bx + bw / 2, y_base + 2, 0.4f, cur ? C_TURQ : C_MUTED, C2D_AlignCenter, "%s", dn);
    }

    /* weight line */
    text(10, 136, 0.45f, C_TEXT, 0, "Weight (kg)");
    int last = -1;
    for (int i = (int)g_weights.count - 1; i >= 0; i--)
        if (strcmp(g_weights.items[i].date, g_day.date) <= 0) {
            last = i;
            break;
        }
    if (last < 0) {
        text(TOP_W / 2, 175, 0.45f, C_MUTED, C2D_AlignCenter, "Log your weight on the Profile page (Y)");
        return;
    }
    int first = last - 9 < 0 ? 0 : last - 9; /* up to 10 points */
    float lo = g_weights.items[last].value, hi = lo;
    for (int i = first; i <= last; i++) {
        float v = g_weights.items[i].value;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    lo -= 0.5f;
    hi += 0.5f;
    float gx0 = 36, gx1 = TOP_W - 24, gy0 = 156, gy1 = 220;
    C2D_DrawRectSolid(gx0, gy1, 0.4f, gx1 - gx0, 1, C_BAR_BG);
    text(gx0 - 4, gy0 - 6, 0.35f, C_MUTED, C2D_AlignRight, "%.1f", hi);
    text(gx0 - 4, gy1 - 6, 0.35f, C_MUTED, C2D_AlignRight, "%.1f", lo);
    int n = last - first + 1;
    float px = 0, py = 0;
    for (int i = 0; i < n; i++) {
        const SeriesPoint *p = &g_weights.items[first + i];
        float tx = n == 1 ? 0.5f : (float)i / (n - 1);
        float x = gx0 + (gx1 - gx0) * tx;
        float y = gy1 - (gy1 - gy0) * (p->value - lo) / (hi - lo);
        if (i > 0)
            C2D_DrawLine(px, py, grad(tx - 1.0f / (n > 1 ? n - 1 : 1)), x, y, grad(tx), 2.5f, 0.45f);
        px = x;
        py = y;
    }
    for (int i = 0; i < n; i++) {
        const SeriesPoint *p = &g_weights.items[first + i];
        float tx = n == 1 ? 0.5f : (float)i / (n - 1);
        float x = gx0 + (gx1 - gx0) * tx;
        float y = gy1 - (gy1 - gy0) * (p->value - lo) / (hi - lo);
        C2D_DrawCircleSolid(x, y, 0.5f, 3.5f, grad(tx));
        if (i == 0 || i == n - 1) {
            text(x, y - 15, 0.38f, C_TEXT, C2D_AlignCenter, "%.1f", p->value);
            text(x, gy1 + 3, 0.35f, C_MUTED, C2D_AlignCenter, "%.5s", p->date + 5);
        }
    }
}

static void draw_workout_picker(void)
{
    header("Add a workout", NULL);
    float kg = weight_for_day();
    for (int i = 0; i < WORKOUT_TYPE_COUNT; i++) {
        float y = 28 + i * 17;
        bool on = i == g_pick_sel;
        if (on) {
            rrect(4, y - 1, TOP_W - 8, 16, 5, C_PANEL2);
            C2D_DrawRectSolid(4, y - 1, 0.45f, 3, 16, grad((float)i / (WORKOUT_TYPE_COUNT - 1)));
        }
        const WorkoutType *t = &WORKOUT_TYPES[i];
        text(16, y, TEXT_SCALE, C_TEXT, 0, "%s", t->name);
        if (t->met > 0 && kg > 0)
            text(TOP_W - 12, y, TEXT_SCALE, C_MUTED, C2D_AlignRight, "about %.0f kcal / hour",
                 workout_kcal(t->met, kg, 60));
    }
}

static void draw_page_activity(void)
{
    if (g_picking) {
        draw_workout_picker();
        return;
    }
    char title[48];
    snprintf(title, sizeof title, "Activity  -  %s", g_day.date);
    header(title, "< >");
    float kg = weight_for_day();

    /* steps card with an hourly chart */
    rrect(8, 28, TOP_W - 16, 88, 10, C_PANEL);
    grad_rect(20, 28, TOP_W - 40, 3, 0, 1);
    text(18, 36, 0.45f, C_MUTED, 0, "Steps");
    text(18, 50, 0.8f, C_TEXT, 0, "%u", g_step_total);
    if (kg > 0) {
        text(TOP_W - 18, 36, 0.45f, C_MUTED, C2D_AlignRight, "about");
        text(TOP_W - 18, 50, 0.7f, C_TURQ, C2D_AlignRight, "%.0f kcal",
             steps_kcal(g_step_total, kg, g_profile.height_cm));
    } else {
        text(TOP_W - 18, 52, 0.45f, C_MUTED, C2D_AlignRight, "Log a weight to see kcal");
    }
    unsigned top = 1;
    for (int h = 0; h < 24; h++)
        if (g_step_hours[h] > top) top = g_step_hours[h];
    float cx = 18, cw = (TOP_W - 36) / 24, base = 108, ch = 26;
    if (g_steps_live) {
        for (int h = 0; h < 24; h++) {
            float bh = ch * g_step_hours[h] / top;
            if (bh > 0)
                C2D_DrawRectSolid(cx + h * cw + 1, base - bh, 0.4f, cw - 2, bh, grad(h / 23.0f));
            else
                C2D_DrawRectSolid(cx + h * cw + 1, base - 1, 0.4f, cw - 2, 1, C_BAR_BG);
        }
        text(cx, base + 1, 0.32f, C_MUTED, 0, "12am");
        text(cx + 12 * cw, base + 1, 0.32f, C_MUTED, 0, "12pm");
    } else {
        text(TOP_W / 2, 86, 0.42f, C_MUTED, C2D_AlignCenter,
             pedometer_available() ? "No steps counted for this day" : "Step counter not available here");
    }

    /* workouts */
    text(10, 124, 0.5f, C_TEXT, 0, "Workouts");
    text(TOP_W - 12, 124, 0.45f, C_TURQ, C2D_AlignRight, "%.0f kcal", workouts_total(&g_workouts));
    if (g_workouts.count == 0) {
        text(TOP_W / 2, 160, 0.45f, C_MUTED, C2D_AlignCenter, "A to add one: weights, running, soccer...");
        return;
    }
    int first = g_workout_sel > 5 ? g_workout_sel - 5 : 0; /* 6 rows fit */
    for (int i = first; i < (int)g_workouts.count && i < first + 6; i++) {
        const Workout *w = &g_workouts.items[i];
        char right[32];
        if (w->minutes > 0)
            snprintf(right, sizeof right, "%.0f min  %.0f kcal", w->minutes, w->kcal);
        else
            snprintf(right, sizeof right, "%.0f kcal", w->kcal);
        food_row(142 + (i - first) * ROW_H, i == g_workout_sel, w->name, NULL, false, right, C_GREEN);
    }
}

static void draw_top_profile(void)
{
    switch (g_page) {
    case PAGE_PROFILE: draw_page_profile(); break;
    case PAGE_ACTIVITY: draw_page_activity(); break;
    case PAGE_REPORT: draw_page_report(); break;
    default: draw_page_charts(); break;
    }
}

/* ---- bottom screen ------------------------------------------------------ */
static void card_bg(void)
{
    rrect(6, 6, BOT_W - 12, 98, 10, C_PANEL);
    grad_rect(18, 6, BOT_W - 36, 3, 0, 1);
}

static void food_card(const Food *f, float grams, bool quick)
{
    card_bg();
    const float W = BOT_W - 28; /* text width inside the card */
    /* one item per line, so nothing can overlap */
    text_fit(14, 11, 0.55f, W, C_TEXT, 0, "%.48s", f->name);
    char sub[96] = "";
    if (!quick && f->brand[0])
        snprintf(sub, sizeof sub, "%.30s", f->brand);
    if (!quick && f->serving_g > 0) {
        size_t n = strlen(sub);
        snprintf(sub + n, sizeof sub - n, "%sServing: %.20s%s%.0f g", n ? "   " : "", f->serving_label,
                 f->serving_label[0] ? " = " : "", f->serving_g);
    }
    if (sub[0])
        text_fit(14, 27, 0.42f, W, C_MUTED, 0, "%s", sub);

    Entry e;
    memset(&e, 0, sizeof e);
    e.food = *f;
    e.grams = quick ? 100 : (grams > 0 ? grams : 100);
    Totals t = entry_totals(&e);
    if (quick)
        text(14, 40, 0.5f, C_TURQ, 0, "%.0f kcal", t.kcal);
    else
        text(14, 40, 0.5f, C_TURQ, 0, "%.0f kcal in %.0f g", t.kcal, e.grams);

    /* the key micronutrients this food lists, on their own line */
    char micros[96] = "";
    int shown = 0;
    for (int i = 0; i < MICRO_KEY_COUNT; i++) {
        if (t.micro[i] <= 0)
            continue;
        char v[16];
        micro_value(v, sizeof v, t.micro[i]);
        size_t n = strlen(micros);
        snprintf(micros + n, sizeof micros - n, "%s%s %s %s", shown ? "   " : "", micro_info((Micro)i)->name, v,
                 micro_info((Micro)i)->unit);
        shown++;
    }
    if (shown > 0)
        text_fit(14, 56, 0.4f, W, C_MUTED, 0, "%s", micros);

    /* three macro chips, green -> turquoise -> blue: name on top, grams below */
    float cw = (BOT_W - 12 - 16 - 16) / 3;
    const char *names[3] = {"Protein", "Carbs", "Fat"};
    float vals[3] = {t.protein, t.carbs, t.fat};
    for (int i = 0; i < 3; i++) {
        float x = 14 + i * (cw + 8);
        rrect_grad(x, 71, cw, 29, 8, x / BOT_W, (x + cw) / BOT_W);
        text(x + cw / 2, 72, 0.4f, C_INK, C2D_AlignCenter, "%s", names[i]);
        text(x + cw / 2, 84, 0.45f, C_INK, C2D_AlignCenter, "%.1f g", vals[i]);
    }
}

static void meal_card(const SavedMeal *m)
{
    card_bg();
    Totals t = meal_totals(m);
    text(14, 12, 0.55f, C_TEXT, 0, "%.34s", m->name);
    text(BOT_W - 14, 12, 0.5f, C_TURQ, C2D_AlignRight, "%.0f kcal", t.kcal);
    text(14, 29, 0.42f, C_MUTED, 0, "P %.0f g   C %.0f g   F %.0f g", t.protein, t.carbs, t.fat);
    for (size_t i = 0; i < m->count && i < 4; i++)
        text(14, 46 + i * 13, 0.42f, C_TEXT, 0, "- %.40s", m->items[i].food.name);
    if (m->count > 4)
        text(14, 46 + 4 * 13, 0.42f, C_MUTED, 0, "  and %d more", (int)m->count - 4);
}

static void hint_card(const char *line1, const char *line2)
{
    card_bg();
    text(BOT_W / 2, 36, 0.5f, C_TEXT, C2D_AlignCenter, "%s", line1);
    text(BOT_W / 2, 58, 0.42f, C_MUTED, C2D_AlignCenter, "%s", line2);
}

static const char *ROW_HELP[PR_COUNT] = {
    "Used for your resting energy (BMR)",
    "A switches between Male and Female",
    "A or Y logs today's weight",
    "In centimeters",
    "Worked out from weight and height",
    "Optional, in percent",
    "A cycles: how active a normal day is",
    "Glasses of 250 ml. ZR / ZL on Today add or remove one",
    "A turns the little sound effects on or off",
    "Your daily calorie goal",
    "Your daily protein goal",
    "Your daily carbs goal",
    "Your daily fat goal",
};

static void draw_profile_details(void)
{
    if (g_page == PAGE_PROFILE) {
        int row = g_sel[TAB_PROFILE];
        hint_card(ROW_LABELS[row], ROW_HELP[row]);
    } else if (g_page == PAGE_REPORT) {
        hint_card("Eaten vs burned", "Burned = resting + basic activity + workouts");
    } else if (g_page == PAGE_ACTIVITY) {
        if (g_picking)
            hint_card("Pick a workout", "Calories use your weight and the minutes");
        else if (!pedometer_available())
            hint_card("Step counter not available", "Workouts still count");
        else
            hint_card("Steps are a heads-up", "Basic activity already covers them");
    } else {
        if (g_week_dirty)
            compute_week();
        Totals avg;
        memset(&avg, 0, sizeof avg);
        int days = 0;
        for (int i = 0; i < WEEK; i++) {
            if (g_week[i].kcal <= 0) continue;
            avg.kcal += g_week[i].kcal;
            avg.protein += g_week[i].protein;
            avg.carbs += g_week[i].carbs;
            avg.fat += g_week[i].fat;
            days++;
        }
        card_bg();
        text(BOT_W / 2, 18, 0.5f, C_TEXT, C2D_AlignCenter, "Average of logged days (%d)", days);
        if (days > 0) {
            text(BOT_W / 2, 40, 0.7f, C_TURQ, C2D_AlignCenter, "%.0f kcal", avg.kcal / days);
            text(BOT_W / 2, 70, 0.45f, C_MUTED, C2D_AlignCenter, "Protein %.0f g   Carbs %.0f g   Fat %.0f g",
                 avg.protein / days, avg.carbs / days, avg.fat / days);
        } else {
            text(BOT_W / 2, 50, 0.45f, C_MUTED, C2D_AlignCenter, "Nothing logged this week");
        }
    }
}

static void draw_details(void)
{
    Tab t = g_tab;
    int count = list_count(t);
    if (g_scanning) {
        hint_card("Scanning for a barcode...", "Works with EAN-13, UPC-A and EAN-8 codes");
        return;
    }
    if (g_unit_picking) {
        food_card(&g_unit_food, g_unit_food.serving_g, false);
        return;
    }
    if (t == TAB_TODAY && g_show_micros) {
        hint_card("From the foods you logged", "Many foods don't list these, so real totals may be higher");
        return;
    }
    switch (t) {
    case TAB_TODAY: {
        Entry *e = selected_entry();
        if (e) food_card(&e->food, e->grams, e->quick);
        else hint_card("Let's eat!", "A to search, Scan a barcode, or tap Quick add");
        break;
    }
    case TAB_SEARCH:
        if (count > 0 && g_pending == PEND_NONE) {
            const Food *f = search_food(g_sel[t]);
            food_card(f, f->serving_g, false);
        } else {
            hint_card("Search Open Food Facts", "Press Y and type a food");
        }
        break;
    case TAB_BRANDS:
        if (g_brand_open >= 0 && g_brand_count > 0 && g_pending == PEND_NONE) {
            const Food *f = &g_brand_foods[g_brand_sel];
            food_card(f, f->serving_g, false);
        } else if (g_brand_open < 0 && count > 0) {
            hint_card(g_brands.names[g_sel[t]], "A to see its products");
        } else {
            hint_card("Your brands", "Y to add a brand");
        }
        break;
    case TAB_FAVS:
        if (count > 0) food_card(&g_favs.items[g_sel[t]].food, g_favs.items[g_sel[t]].grams, false);
        else hint_card("Favorites log in one tap", "Press X on a food to add it here");
        break;
    case TAB_MEALS:
        if (g_building) {
            if (count > 0) {
                const Entry *e = &g_draft.items[g_sel[t]];
                food_card(&e->food, e->grams, e->quick);
            } else {
                hint_card("Building a meal", "Y opens Search. A on a food adds it here");
            }
        } else if (count > 0) {
            meal_card(&g_meals.items[g_sel[t]]);
        } else {
            hint_card("Meals log several foods at once", "Tap + New meal to make one");
        }
        break;
    default:
        draw_profile_details();
        break;
    }
}

static void draw_bottom(void)
{
    draw_details();

    for (int i = 0; i < g_button_count; i++) {
        Button *b = &g_buttons[i];
        bool g = b->color == C_GRAD;
        if (g)
            rrect_grad(b->x, b->y, b->w, b->h, 9, b->x / BOT_W, (b->x + b->w) / BOT_W);
        else
            rrect(b->x, b->y, b->w, b->h, 9, b->color);
        text(b->x + b->w / 2, b->y + 6, 0.45f, g ? C_INK : C_TEXT, C2D_AlignCenter, "%s", b->label);
    }

    bool row = false;
    if (slot_row_visible()) {
        u32 mc = meal_color(g_slot);
        rrect(8, SLOT_Y, BOT_W - 16, 18, 9, C_PANEL);
        text(20, SLOT_Y + 2, 0.45f, mc, 0, "<");
        text(BOT_W - 20, SLOT_Y + 2, 0.45f, mc, C2D_AlignRight, ">");
        text(BOT_W / 2, SLOT_Y + 2, 0.45f, C_TEXT, C2D_AlignCenter, "Adding to: %s", meal_name(g_slot));
        row = true;
    } else if (build_strip_visible()) {
        rrect_grad(8, SLOT_Y, BOT_W - 16, 18, 9, 0, 1);
        text(BOT_W / 2, SLOT_Y + 2, 0.45f, C_INK, C2D_AlignCenter, "Building \"%.18s\": %d foods%s",
             g_draft.name, (int)g_draft.count, g_tab == TAB_MEALS ? "" : "  (tap to finish)");
        row = true;
    }

    if (g_status[0])
        text(BOT_W / 2, SLOT_Y - 14 + (row ? 0 : 14), 0.4f, C_MINT, C2D_AlignCenter, "%.64s", g_status);

    /* tab bar */
    float tw = BOT_W / TAB_COUNT;
    C2D_DrawRectSolid(0, TAB_Y, 0.4f, BOT_W, SCREEN_H - TAB_Y, C_PANEL);
    for (int i = 0; i < TAB_COUNT; i++) {
        float x = i * tw;
        bool on = (Tab)i == g_tab;
        if (on)
            rrect_grad(x + 2, TAB_Y + 4, tw - 4, SCREEN_H - TAB_Y - 8, 10, x / BOT_W, (x + tw) / BOT_W);
        else
            C2D_DrawCircleSolid(x + tw / 2, TAB_Y + 9, 0.45f, 3, grad((x + tw / 2) / BOT_W));
        text(x + tw / 2, TAB_Y + (on ? 12 : 16), 0.42f, on ? C_INK : C_MUTED, C2D_AlignCenter, "%s",
             TAB_NAMES[i]);
    }
}

/* ---- main --------------------------------------------------------------- */
int main(void)
{
    romfsInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_textbuf = C2D_TextBufNew(8192);
    init_colors();

    g_net_ok = net_init() == 0;
    if (storage_init(DATA_DIR) != 0)
        status("Could not create " DATA_DIR);
    targets_load(&g_targets);
    foods_load(&g_cache);
    favs_load(&g_favs);
    meals_load(&g_meals);
    brands_load(&g_brands);
    profile_load(&g_profile);
    series_load(&g_weights, WEIGHTS_FILE);
    series_load(&g_steps, STEPS_FILE);
    series_load(&g_water, WATER_FILE);
    pedometer_init();
    sound_init();
    sound_enable(!g_profile.sounds_off);
    date_today(g_today);
    load_day(g_today);
    compute_streak();
    g_slot = meal_for_now();
    if (!g_net_ok)
        status("No network: saved foods and favorites still work");

    while (aptMainLoop()) {
        hidScanInput();
        /* steps keep coming in while the app is open */
        static unsigned frames = 0;
        if (++frames % 600 == 0 && g_tab == TAB_PROFILE && !g_picking)
            refresh_activity();
        u32 down = hidKeysDown();
        layout_buttons();
        if (!handle_keys(down))
            break;
        if (down & KEY_TOUCH)
            handle_touch();
        layout_buttons(); /* the tab or state may have changed */
        g_anim++;

        if (g_scanning) {
            char code[BARCODE_LEN];
            if (scanner_frame(code)) {
                stop_scan();
                sound_play(SND_SCAN);
                memcpy(g_barcode, code, BARCODE_LEN);
                status("Got %s, looking it up...", code);
                g_pending = PEND_PRODUCT;
                layout_buttons();
            }
        }

        C2D_TextBufClear(g_textbuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, C_BG);
        C2D_SceneBegin(top);
        if (g_scanning) {
            draw_top_scanner();
        } else if (g_unit_picking) {
            draw_unit_picker();
        } else switch (g_tab) {
        case TAB_TODAY:
            if (g_show_micros) draw_top_micros();
            else draw_top_today();
            break;
        case TAB_SEARCH: draw_top_search(); break;
        case TAB_BRANDS: draw_top_brands(); break;
        case TAB_FAVS: draw_top_favs(); break;
        case TAB_MEALS: draw_top_meals(); break;
        default: draw_top_profile(); break;
        }
        C2D_TargetClear(bottom, C_BG);
        C2D_SceneBegin(bottom);
        draw_bottom();
        C3D_FrameEnd(0);

        /* Network calls run after a frame that says "Loading..." */
        if (g_pending == PEND_SEARCH) {
            run_search();
            g_pending = PEND_NONE;
        } else if (g_pending == PEND_BRAND) {
            fetch_brand();
            g_pending = PEND_NONE;
        } else if (g_pending == PEND_PRODUCT) {
            lookup_product();
            g_pending = PEND_NONE;
        }
    }

    stop_scan();
    sound_exit();
    C2D_TextBufDelete(g_textbuf);
    net_exit();
    pedometer_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
    return 0;
}
