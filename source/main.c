/* Cal Pop: a colorful calorie and macro tracker for the 3DS.
 *
 * Top screen:    the current tab's content (today's log, search results,
 *                brand tiles and products, favorites, saved meals).
 * Bottom screen: details of the selected item, touch buttons, and a tab bar.
 * Every touch button also has a button shortcut, shown on its label.
 */
#include <3ds.h>
#include <citro2d.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/date.h"
#include "core/food.h"
#include "core/off.h"
#include "core/storage.h"
#include "net.h"

#define DATA_DIR "sdmc:/3ds/caltracker" /* shared with the plain Cal Tracker */
#define TOP_W 400.0f
#define BOT_W 320.0f
#define SCREEN_H 240.0f
#define ROW_H 15.0f
#define TEXT_SCALE 0.5f
#define MAX_BRAND_FOODS 60

/* ---- palette (bright, playful) ------------------------------------------ */
static u32 C_BG, C_PANEL, C_PANEL2, C_TEXT, C_INK, C_MUTED, C_BAR_BG;
static u32 C_PINK, C_ORANGE, C_YELLOW, C_TEAL, C_LIME, C_BLUE, C_RED;
static u32 C_PROTEIN, C_CARBS, C_FAT;

static void init_colors(void)
{
    C_BG = C2D_Color32(0x2a, 0x16, 0x50, 0xff);     /* deep grape */
    C_PANEL = C2D_Color32(0x3d, 0x22, 0x72, 0xff);  /* lighter grape */
    C_PANEL2 = C2D_Color32(0x4e, 0x2e, 0x8c, 0xff); /* selection */
    C_TEXT = C2D_Color32(0xff, 0xff, 0xff, 0xff);
    C_INK = C2D_Color32(0x1d, 0x0f, 0x38, 0xff); /* dark text on bright fills */
    C_MUTED = C2D_Color32(0xcd, 0xb8, 0xf5, 0xff);
    C_BAR_BG = C2D_Color32(0x55, 0x3a, 0x8f, 0xff);
    C_PINK = C2D_Color32(0xff, 0x4f, 0xa3, 0xff);
    C_ORANGE = C2D_Color32(0xff, 0x8a, 0x1f, 0xff);
    C_YELLOW = C2D_Color32(0xff, 0xd2, 0x3f, 0xff);
    C_TEAL = C2D_Color32(0x22, 0xd3, 0xc5, 0xff);
    C_LIME = C2D_Color32(0xa3, 0xe6, 0x35, 0xff);
    C_BLUE = C2D_Color32(0x4d, 0x8c, 0xff, 0xff);
    C_RED = C2D_Color32(0xff, 0x5a, 0x4e, 0xff);
    C_PROTEIN = C_BLUE;
    C_CARBS = C_ORANGE;
    C_FAT = C_PINK;
}

/* ---- tabs and actions --------------------------------------------------- */
typedef enum { TAB_TODAY, TAB_SEARCH, TAB_BRANDS, TAB_FAVS, TAB_MEALS, TAB_COUNT } Tab;
static const char *TAB_NAMES[TAB_COUNT] = {"Today", "Search", "Brands", "Favs", "Meals"};

static u32 tab_color(Tab t)
{
    switch (t) {
    case TAB_TODAY: return C_ORANGE;
    case TAB_SEARCH: return C_TEAL;
    case TAB_BRANDS: return C_PINK;
    case TAB_FAVS: return C_YELLOW;
    default: return C_LIME;
    }
}

typedef enum {
    ACT_NONE,
    ACT_GO_SEARCH,
    ACT_LOG,
    ACT_LOG_GRAMS,
    ACT_STAR,
    ACT_SEARCH,
    ACT_QUICK,
    ACT_SAVE_MEAL,
    ACT_TARGETS,
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
} Action;

typedef struct {
    float x, y, w, h;
    const char *label;
    u32 color;
    Action action;
} Button;

#define MAX_BUTTONS 10
static Button g_buttons[MAX_BUTTONS];
static int g_button_count;

/* ---- app state ---------------------------------------------------------- */
static DayLog g_day;
static FoodCache g_cache;
static Targets g_targets;
static FavList g_favs;
static MealList g_meals;
static BrandList g_brands;
static char g_today[CT_DATE_LEN];

static Tab g_tab = TAB_TODAY;
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

typedef enum { PEND_NONE, PEND_SEARCH, PEND_BRAND } Pending;
static Pending g_pending = PEND_NONE;
static char g_status[96] = "";
static bool g_net_ok = false;

static size_t g_order[CT_MAX_ENTRIES]; /* today's entries grouped by meal */
static C2D_TextBuf g_textbuf;

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

static void bar(float x, float y, float w, float h, float value, float target, u32 color)
{
    rrect(x, y, w, h, h / 2, C_BAR_BG);
    if (target <= 0 || value <= 0)
        return;
    float frac = value / target;
    u32 c = frac > 1.0f ? C_RED : color;
    if (frac > 1.0f)
        frac = 1.0f;
    float fw = w * frac;
    if (fw < h)
        fw = h; /* keep the rounded end visible */
    rrect(x, y, fw, h, h / 2, c);
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

static void load_day(const char *date)
{
    if (day_load(date, &g_day) != 0)
        status("Could not read the log for %s", date);
    rebuild_order();
    reset_list(TAB_TODAY);
}

static void save_day(void)
{
    if (day_save(&g_day) != 0)
        status("Could not save to the SD card");
    rebuild_order();
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

/* Log a food with a given weight to the current meal slot. */
static bool log_food_grams(const Food *food, float grams)
{
    Entry e;
    memset(&e, 0, sizeof e);
    e.food = *food;
    e.grams = grams;
    e.meal = g_slot;
    if (!add_entry(&e))
        return false;
    save_day();
    foods_touch(&g_cache, food);
    foods_save(&g_cache);
    status("Added %.0f g %.24s (%.0f kcal) to %s", grams, food->name, entry_totals(&e).kcal,
           meal_name(g_slot));
    return true;
}

static bool log_food_ask(const Food *food)
{
    float grams = food->serving_g > 0 ? food->serving_g : 100.0f;
    if (!prompt_number("Grams eaten", grams, &grams) || grams <= 0)
        return false;
    return log_food_grams(food, grams);
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

/* ---- selection helpers -------------------------------------------------- */
static int visible_rows(Tab t)
{
    return t == TAB_TODAY ? 10 : 13;
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
    default: return (int)g_meals.count;
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

static void fetch_brand(void)
{
    const char *brand = g_brands.names[g_brand_open];
    char url[512];
    char err[96] = "";
    for (int ep = 0; ep < OFF_ENDPOINT_COUNT; ep++) {
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
        if (n == 0 && ep + 1 < OFF_ENDPOINT_COUNT)
            continue; /* the other endpoint filters by brand more strictly */
        g_brand_count = n;
        brand_cache_save(brand, g_brand_foods, n);
        status(n ? "%d %.20s products" : "No products found for %.20s", n, brand);
        return;
    }
    status("Could not load %.20s: %s", brand, err);
}

static void open_brand(int index)
{
    g_brand_open = index;
    g_brand_sel = 0;
    g_brand_scroll = 0;
    int n = brand_cache_load(g_brands.names[index], g_brand_foods, MAX_BRAND_FOODS);
    if (n >= 0) {
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
    e.meal = g_slot;
    if (!add_entry(&e))
        return;
    save_day();
    status("Quick added %.0f kcal to %s", kcal, meal_name(g_slot));
}

static void save_as_meal(void)
{
    Entry *sel = selected_entry();
    if (!sel) {
        status("Log some food first, then save it as a meal");
        return;
    }
    Meal slot = sel->meal;
    SavedMeal m;
    memset(&m, 0, sizeof m);
    for (size_t i = 0; i < g_day.count && m.count < CT_MAX_MEAL_ITEMS; i++)
        if (g_day.entries[i].meal == slot)
            m.items[m.count++] = g_day.entries[i];
    char def[CT_NAME_LEN];
    snprintf(def, sizeof def, "My %s", meal_name(slot));
    if (!prompt_text("Name this meal", def, m.name, sizeof m.name))
        return;
    if (meals_add(&g_meals, &m) != 0) {
        status("Saved meals are full (%d)", CT_MAX_SAVED_MEALS);
        return;
    }
    meals_save(&g_meals);
    status("Saved \"%.24s\" with %d foods", m.name, (int)m.count);
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
        }
        if (!add_entry(&e))
            break;
        added++;
    }
    save_day();
    status("Logged \"%.24s\" (%d foods) to %s", m->name, added, meal_name(g_slot));
}

static void edit_targets(void)
{
    Targets t = g_targets;
    if (!prompt_number("Daily calories", t.kcal, &t.kcal)) return;
    if (!prompt_number("Daily protein (g)", t.protein, &t.protein)) return;
    if (!prompt_number("Daily carbs (g)", t.carbs, &t.carbs)) return;
    if (!prompt_number("Daily fat (g)", t.fat, &t.fat)) return;
    g_targets = t;
    status(targets_save(&g_targets) == 0 ? "Targets saved" : "Could not save targets");
}

static void switch_tab(Tab t)
{
    g_tab = t;
    if (g_status[0] == 'L' && strncmp(g_status, "Loading", 7) == 0)
        g_status[0] = '\0';
}

static void do_action(Action a)
{
    Tab t = g_tab;
    int count = list_count(t);
    int *sel = &g_sel[t];
    char date[CT_DATE_LEN];

    switch (a) {
    case ACT_NONE:
        break;
    case ACT_GO_SEARCH:
        switch_tab(TAB_SEARCH);
        break;
    case ACT_MEAL_PREV:
        g_slot = (Meal)((g_slot + MEAL_COUNT - 1) % MEAL_COUNT);
        break;
    case ACT_MEAL_NEXT:
        g_slot = (Meal)((g_slot + 1) % MEAL_COUNT);
        break;
    case ACT_PREV_DAY:
        if (date_add_days(g_day.date, -1, date) == 0) load_day(date);
        break;
    case ACT_NEXT_DAY:
        if (date_add_days(g_day.date, 1, date) == 0) load_day(date);
        break;
    case ACT_TODAY:
        load_day(g_today);
        break;
    case ACT_TARGETS:
        edit_targets();
        break;
    case ACT_QUICK:
        quick_entry();
        break;
    case ACT_SAVE_MEAL:
        save_as_meal();
        break;
    case ACT_DELETE:
        if (t == TAB_TODAY && count > 0) {
            Entry *e = selected_entry();
            status("Removed %.30s", e->food.name);
            day_remove(&g_day, g_order[*sel]);
            save_day();
            if (*sel >= (int)g_day.count) move_sel(sel, &g_scroll[t], -1, (int)g_day.count, visible_rows(t));
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
            float grams = e->grams;
            if (prompt_number("Grams eaten", grams, &grams) && grams > 0) {
                e->grams = grams;
                save_day();
                status("Updated %.30s", e->food.name);
            }
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
                status("Quick entries can't be favorites. Save it as a meal instead");
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
            log_food_ask(search_food(*sel));
        } else if (t == TAB_BRANDS && g_brand_open >= 0 && g_brand_count > 0) {
            log_food_ask(&g_brand_foods[g_brand_sel]);
        } else if (t == TAB_FAVS && count > 0) {
            Favorite f = g_favs.items[*sel];
            log_food_grams(&f.food, f.grams);
        } else if (t == TAB_MEALS && count > 0) {
            log_saved_meal(&g_meals.items[*sel], 1.0f);
        }
        break;
    case ACT_LOG_GRAMS:
        if (t == TAB_FAVS && count > 0) {
            Favorite f = g_favs.items[*sel];
            float grams = f.grams;
            if (prompt_number("Grams eaten", grams, &grams) && grams > 0)
                log_food_grams(&f.food, grams);
        }
        break;
    case ACT_LOG_SCALED:
        if (t == TAB_MEALS && count > 0) {
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

    int *sel = (t == TAB_BRANDS) ? &g_brand_sel : &g_sel[t];
    int *scroll = (t == TAB_BRANDS) ? &g_brand_scroll : &g_scroll[t];
    if (down & KEY_DUP) move_sel(sel, scroll, -1, count, visible_rows(t));
    if (down & KEY_DDOWN) move_sel(sel, scroll, 1, count, visible_rows(t));

    if (t == TAB_TODAY) {
        if (down & KEY_DLEFT) do_action(ACT_PREV_DAY);
        if (down & KEY_DRIGHT) do_action(ACT_NEXT_DAY);
        if (down & KEY_A) do_action(ACT_GO_SEARCH);
        if (down & KEY_B) do_action(ACT_TODAY);
        if (down & KEY_X) do_action(ACT_DELETE);
        if (down & KEY_Y) do_action(ACT_EDIT_GRAMS);
        if (down & KEY_SELECT) do_action(ACT_TARGETS);
        return true;
    }

    if (down & KEY_DLEFT) do_action(ACT_MEAL_PREV);
    if (down & KEY_DRIGHT) do_action(ACT_MEAL_NEXT);
    if (down & KEY_A) do_action(ACT_LOG);
    if (down & KEY_B) do_action(ACT_BACK);
    switch (t) {
    case TAB_SEARCH:
        if (down & KEY_Y) do_action(ACT_SEARCH);
        if (down & KEY_X) do_action(ACT_STAR);
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

static void layout_buttons(void)
{
    g_button_count = 0;
    float w3 = (BOT_W - 8 * 4) / 3; /* three per row */
    float x0 = 8, x1 = 8 + w3 + 8, x2 = 8 + 2 * (w3 + 8);
    float r0 = BTN_Y, r1 = BTN_Y + BTN_H + 5;

    switch (g_tab) {
    case TAB_TODAY:
        add_button(x0, r0, w3, BTN_H, "Quick add", C_ORANGE, ACT_QUICK);
        add_button(x1, r0, w3, BTN_H, "Save as meal", C_LIME, ACT_SAVE_MEAL);
        add_button(x2, r0, w3, BTN_H, "Fav it", C_YELLOW, ACT_STAR);
        add_button(x0, r1, w3, BTN_H, "< Day", C_PANEL2, ACT_PREV_DAY);
        add_button(x1, r1, w3, BTN_H, "Targets", C_PANEL2, ACT_TARGETS);
        add_button(x2, r1, w3, BTN_H, "Day >", C_PANEL2, ACT_NEXT_DAY);
        break;
    case TAB_SEARCH:
        add_button(x0, r0, w3, BTN_H, "Y  Search", C_TEAL, ACT_SEARCH);
        add_button(x1, r0, w3, BTN_H, "A  Log", C_LIME, ACT_LOG);
        add_button(x2, r0, w3, BTN_H, "X  Fav", C_YELLOW, ACT_STAR);
        if (g_have_results)
            add_button(x0, r1, w3, BTN_H, "B  Recent", C_PANEL2, ACT_BACK);
        break;
    case TAB_BRANDS:
        if (g_brand_open < 0) {
            add_button(x0, r0, w3, BTN_H, "Y  Add brand", C_PINK, ACT_ADD_BRAND);
            add_button(x1, r0, w3, BTN_H, "A  Open", C_LIME, ACT_OPEN_BRAND);
            add_button(x2, r0, w3, BTN_H, "X  Remove", C_PANEL2, ACT_REMOVE_BRAND);
        } else {
            add_button(x0, r0, w3, BTN_H, "A  Log", C_LIME, ACT_LOG);
            add_button(x1, r0, w3, BTN_H, "X  Fav", C_YELLOW, ACT_STAR);
            add_button(x2, r0, w3, BTN_H, "Y  Refresh", C_PINK, ACT_REFRESH);
            add_button(x0, r1, w3, BTN_H, "B  Brands", C_PANEL2, ACT_BACK);
        }
        break;
    case TAB_FAVS:
        add_button(x0, r0, w3, BTN_H, "A  Log", C_LIME, ACT_LOG);
        add_button(x1, r0, w3, BTN_H, "Y  Other grams", C_TEAL, ACT_LOG_GRAMS);
        add_button(x2, r0, w3, BTN_H, "X  Unfav", C_PANEL2, ACT_STAR);
        add_button(x0, r1, w3, BTN_H, "Usual grams", C_PANEL2, ACT_EDIT_GRAMS);
        break;
    case TAB_MEALS:
        add_button(x0, r0, w3, BTN_H, "A  Log meal", C_LIME, ACT_LOG);
        add_button(x1, r0, w3, BTN_H, "Y  Half/double", C_TEAL, ACT_LOG_SCALED);
        add_button(x2, r0, w3, BTN_H, "X  Delete", C_PANEL2, ACT_DELETE);
        break;
    default:
        break;
    }
}

/* Meal slot picker row, on every tab that logs food. */
#define SLOT_Y 183.0f
static bool slot_row_visible(void)
{
    return !(g_tab == TAB_BRANDS && g_brand_open < 0);
}

static void handle_touch(void)
{
    touchPosition tp;
    hidTouchRead(&tp);
    float x = tp.px, y = tp.py;

    if (y >= TAB_Y) {
        int t = (int)(x / (BOT_W / TAB_COUNT));
        if (t >= 0 && t < TAB_COUNT)
            switch_tab((Tab)t);
        return;
    }
    if (slot_row_visible() && y >= SLOT_Y - 2 && y < SLOT_Y + 18) {
        if (x < 60) do_action(ACT_MEAL_PREV);
        else if (x > BOT_W - 60) do_action(ACT_MEAL_NEXT);
        return;
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
static void header(const char *title, u32 color)
{
    C2D_DrawRectSolid(0, 0, 0.4f, TOP_W, 22, color);
    text(10, 3, 0.55f, C_INK, 0, "%s", title);
}

static void fav_dot(float x, float y)
{
    C2D_DrawCircleSolid(x, y, 0.5f, 3.5f, C_YELLOW);
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
    if (brand && brand[0])
        text(20, y, TEXT_SCALE, C_TEXT, 0, "%.28s  (%.14s)", name, brand);
    else
        text(20, y, TEXT_SCALE, C_TEXT, 0, "%.40s", name);
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
    switch (m) {
    case MEAL_BREAKFAST: return C_YELLOW;
    case MEAL_LUNCH: return C_TEAL;
    case MEAL_DINNER: return C_PINK;
    default: return C_LIME;
    }
}

static void draw_top_today(void)
{
    Totals t = sum_entries(g_day.entries, g_day.count, -1);
    bool is_today = strcmp(g_day.date, g_today) == 0;
    C2D_DrawRectSolid(0, 0, 0.4f, TOP_W, 70, C_PANEL);
    text(10, 6, 0.6f, C_TEXT, 0, "%s%s", g_day.date, is_today ? "  (today)" : "");
    text(TOP_W - 10, 6, 0.6f, t.kcal > g_targets.kcal ? C_RED : C_LIME, C2D_AlignRight,
         "%.0f / %.0f kcal", t.kcal, g_targets.kcal);
    bar(10, 28, TOP_W - 20, 9, t.kcal, g_targets.kcal, C_LIME);

    float col = (TOP_W - 20) / 3.0f;
    text(10, 41, 0.45f, C_TEXT, 0, "Protein %.0f/%.0f g", t.protein, g_targets.protein);
    text(10 + col, 41, 0.45f, C_TEXT, 0, "Carbs %.0f/%.0f g", t.carbs, g_targets.carbs);
    text(10 + 2 * col, 41, 0.45f, C_TEXT, 0, "Fat %.0f/%.0f g", t.fat, g_targets.fat);
    bar(10, 57, col - 8, 6, t.protein, g_targets.protein, C_PROTEIN);
    bar(10 + col, 57, col - 8, 6, t.carbs, g_targets.carbs, C_CARBS);
    bar(10 + 2 * col, 57, col - 8, 6, t.fat, g_targets.fat, C_FAT);

    float y = 76;
    if (g_day.count == 0) {
        rrect(40, y + 30, TOP_W - 80, 60, 12, C_PANEL);
        text(TOP_W / 2, y + 40, 0.6f, C_YELLOW, C2D_AlignCenter, "Nothing logged yet!");
        text(TOP_W / 2, y + 62, 0.45f, C_MUTED, C2D_AlignCenter, "A to search, or tap Quick add");
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
        else
            snprintf(right, sizeof right, "%.0f g  %.0f kcal", e->grams, et.kcal);
        float ry = y + row * ROW_H;
        food_row(ry, idx == g_sel[TAB_TODAY], e->food.name, e->quick ? "" : e->food.brand,
                 !e->quick && is_fav(&e->food), right, meal_color(e->meal));
        /* meal color tag on the far left */
        C2D_DrawRectSolid(4, ry + 2, 0.5f, 3, ROW_H - 5, meal_color(e->meal));
    }
    scroll_hint(y + rows * ROW_H + 1, g_scroll[TAB_TODAY], rows, (int)g_day.count);
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

static void empty_card(const char *title, const char *hint, u32 color)
{
    rrect(40, 80, TOP_W - 80, 64, 12, C_PANEL);
    text(TOP_W / 2, 90, 0.6f, color, C2D_AlignCenter, "%s", title);
    text(TOP_W / 2, 114, 0.45f, C_MUTED, C2D_AlignCenter, "%s", hint);
}

static void draw_top_search(void)
{
    char title[96];
    if (g_have_results)
        snprintf(title, sizeof title, "Search: %.40s", g_query);
    else
        snprintf(title, sizeof title, "Search  -  recent foods");
    header(title, C_TEAL);
    if (g_pending == PEND_SEARCH) {
        empty_card("Searching...", "Asking Open Food Facts", C_TEAL);
        return;
    }
    int count = list_count(TAB_SEARCH);
    if (count == 0) {
        empty_card(g_have_results ? "No results" : "Find a food", "Press Y to search online", C_TEAL);
        return;
    }
    draw_food_list(g_have_results ? g_results : g_cache.items, count, g_sel[TAB_SEARCH],
                   g_scroll[TAB_SEARCH], 28, C_TEAL);
}

static void draw_top_brands(void)
{
    if (g_brand_open >= 0) {
        char title[64];
        snprintf(title, sizeof title, "Brands  >  %.30s", g_brands.names[g_brand_open]);
        header(title, C_PINK);
        if (g_pending == PEND_BRAND) {
            empty_card("Loading products...", "Asking Open Food Facts", C_PINK);
            return;
        }
        if (g_brand_count == 0) {
            empty_card("No products yet", "Press Y to refresh", C_PINK);
            return;
        }
        draw_food_list(g_brand_foods, g_brand_count, g_brand_sel, g_brand_scroll, 28, C_PINK);
        return;
    }

    header("My Brands", C_PINK);
    if (g_brands.count == 0) {
        empty_card("Follow your favorite brands", "Press Y to add one, like Quest or Barebells", C_PINK);
        return;
    }
    static const int ncolors = 6;
    u32 colors[6] = {C_PINK, C_ORANGE, C_YELLOW, C_TEAL, C_LIME, C_BLUE};
    float tw = (TOP_W - 16 - 2 * 8) / 3, th = 30;
    int sel = g_sel[TAB_BRANDS];
    /* scroll by rows of 3 so the selected tile stays on screen (6 rows fit) */
    int first_row = sel / 3 >= 6 ? sel / 3 - 5 : 0;
    for (int i = first_row * 3; i < (int)g_brands.count && i < (first_row + 6) * 3; i++) {
        int r = i / 3 - first_row, c = i % 3;
        float x = 8 + c * (tw + 8), y = 30 + r * (th + 4);
        u32 col = colors[i % ncolors];
        if (i == sel) {
            rrect(x - 2, y - 2, tw + 4, th + 4, 10, C_TEXT);
        }
        rrect(x, y, tw, th, 9, col);
        text(x + tw / 2, y + 7, 0.55f, C_INK, C2D_AlignCenter, "%.16s", g_brands.names[i]);
    }
}

static void draw_top_favs(void)
{
    header("Favorites", C_YELLOW);
    if (g_favs.count == 0) {
        empty_card("No favorites yet", "Press X on any food to make it a favorite", C_YELLOW);
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
                 C_YELLOW);
    }
    scroll_hint(28 + rows * ROW_H + 1, g_scroll[TAB_FAVS], rows, (int)g_favs.count);
}

static Totals meal_totals(const SavedMeal *m)
{
    return sum_entries(m->items, m->count, -1);
}

static void draw_top_meals(void)
{
    header("Saved meals", C_LIME);
    if (g_meals.count == 0) {
        empty_card("No saved meals yet", "On Today, select a food and tap Save as meal", C_LIME);
        return;
    }
    int rows = visible_rows(TAB_MEALS);
    for (int row = 0; row < rows && g_scroll[TAB_MEALS] + row < (int)g_meals.count; row++) {
        int idx = g_scroll[TAB_MEALS] + row;
        const SavedMeal *m = &g_meals.items[idx];
        char right[32], items[16];
        snprintf(right, sizeof right, "%.0f kcal", meal_totals(m).kcal);
        snprintf(items, sizeof items, "%d foods", (int)m->count);
        food_row(28 + row * ROW_H, idx == g_sel[TAB_MEALS], m->name, items, false, right, C_LIME);
    }
    scroll_hint(28 + rows * ROW_H + 1, g_scroll[TAB_MEALS], rows, (int)g_meals.count);
}

/* ---- bottom screen ------------------------------------------------------ */
static void food_card(const Food *f, float grams, bool quick)
{
    rrect(6, 6, BOT_W - 12, 98, 10, C_PANEL);
    text(14, 12, 0.55f, C_TEXT, 0, "%.34s", f->name);
    if (!quick && f->brand[0])
        text(14, 29, 0.45f, C_MUTED, 0, "%.40s", f->brand);
    Entry e;
    memset(&e, 0, sizeof e);
    e.food = *f;
    e.grams = quick ? 100 : (grams > 0 ? grams : 100);
    Totals t = entry_totals(&e);
    if (quick)
        text(14, 47, 0.5f, C_YELLOW, 0, "%.0f kcal", t.kcal);
    else
        text(14, 47, 0.5f, C_YELLOW, 0, "%.0f kcal in %.0f g", t.kcal, e.grams);

    /* three macro chips */
    float cw = (BOT_W - 12 - 16 - 16) / 3;
    const char *names[3] = {"Protein", "Carbs", "Fat"};
    float vals[3] = {t.protein, t.carbs, t.fat};
    u32 cols[3] = {C_PROTEIN, C_CARBS, C_FAT};
    for (int i = 0; i < 3; i++) {
        float x = 14 + i * (cw + 8);
        rrect(x, 70, cw, 26, 8, cols[i]);
        text(x + cw / 2, 75, 0.45f, C_INK, C2D_AlignCenter, "%s %.1f g", names[i], vals[i]);
    }
}

static void meal_card(const SavedMeal *m)
{
    rrect(6, 6, BOT_W - 12, 98, 10, C_PANEL);
    Totals t = meal_totals(m);
    text(14, 12, 0.55f, C_TEXT, 0, "%.34s", m->name);
    text(BOT_W - 14, 12, 0.5f, C_YELLOW, C2D_AlignRight, "%.0f kcal", t.kcal);
    text(14, 29, 0.42f, C_MUTED, 0, "P %.0f g   C %.0f g   F %.0f g", t.protein, t.carbs, t.fat);
    for (size_t i = 0; i < m->count && i < 4; i++)
        text(14, 46 + i * 13, 0.42f, C_TEXT, 0, "- %.40s", m->items[i].food.name);
    if (m->count > 4)
        text(14, 46 + 4 * 13, 0.42f, C_MUTED, 0, "  and %d more", (int)m->count - 4);
}

static void hint_card(const char *line1, const char *line2)
{
    rrect(6, 6, BOT_W - 12, 98, 10, C_PANEL);
    text(BOT_W / 2, 36, 0.5f, C_TEXT, C2D_AlignCenter, "%s", line1);
    text(BOT_W / 2, 58, 0.42f, C_MUTED, C2D_AlignCenter, "%s", line2);
}

static void draw_details(void)
{
    Tab t = g_tab;
    int count = list_count(t);
    switch (t) {
    case TAB_TODAY: {
        Entry *e = selected_entry();
        if (e) food_card(&e->food, e->grams, e->quick);
        else hint_card("Let's eat!", "A to search foods, or tap Quick add");
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
    default:
        if (count > 0) meal_card(&g_meals.items[g_sel[t]]);
        else hint_card("Meals log several foods at once", "Save one from the Today tab");
        break;
    }
}

static void draw_bottom(void)
{
    draw_details();

    for (int i = 0; i < g_button_count; i++) {
        Button *b = &g_buttons[i];
        rrect(b->x, b->y, b->w, b->h, 9, b->color);
        u32 ink = (b->color == C_PANEL2) ? C_TEXT : C_INK;
        text(b->x + b->w / 2, b->y + 6, 0.45f, ink, C2D_AlignCenter, "%s", b->label);
    }

    if (slot_row_visible()) {
        u32 mc = meal_color(g_slot);
        rrect(8, SLOT_Y, BOT_W - 16, 18, 9, C_PANEL);
        text(20, SLOT_Y + 2, 0.45f, mc, 0, "<");
        text(BOT_W - 20, SLOT_Y + 2, 0.45f, mc, C2D_AlignRight, ">");
        text(BOT_W / 2, SLOT_Y + 2, 0.45f, C_TEXT, C2D_AlignCenter, "Adding to: %s", meal_name(g_slot));
    }

    if (g_status[0])
        text(BOT_W / 2, SLOT_Y - 14 + (slot_row_visible() ? 0 : 14), 0.4f, C_YELLOW, C2D_AlignCenter,
             "%.64s", g_status);

    /* tab bar */
    float tw = BOT_W / TAB_COUNT;
    C2D_DrawRectSolid(0, TAB_Y, 0.4f, BOT_W, SCREEN_H - TAB_Y, C_PANEL);
    for (int i = 0; i < TAB_COUNT; i++) {
        float x = i * tw;
        bool on = (Tab)i == g_tab;
        if (on)
            rrect(x + 3, TAB_Y + 4, tw - 6, SCREEN_H - TAB_Y - 8, 10, tab_color((Tab)i));
        else
            C2D_DrawCircleSolid(x + tw / 2, TAB_Y + 9, 0.45f, 3, tab_color((Tab)i));
        text(x + tw / 2, TAB_Y + (on ? 12 : 16), 0.48f, on ? C_INK : C_MUTED, C2D_AlignCenter, "%s",
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
    date_today(g_today);
    load_day(g_today);
    g_slot = meal_for_now();
    if (!g_net_ok)
        status("No network: saved foods and favorites still work");

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        layout_buttons();
        if (!handle_keys(down))
            break;
        if (down & KEY_TOUCH)
            handle_touch();
        layout_buttons(); /* the tab or state may have changed */

        C2D_TextBufClear(g_textbuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, C_BG);
        C2D_SceneBegin(top);
        switch (g_tab) {
        case TAB_TODAY: draw_top_today(); break;
        case TAB_SEARCH: draw_top_search(); break;
        case TAB_BRANDS: draw_top_brands(); break;
        case TAB_FAVS: draw_top_favs(); break;
        default: draw_top_meals(); break;
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
        }
    }

    C2D_TextBufDelete(g_textbuf);
    net_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    romfsExit();
    return 0;
}
