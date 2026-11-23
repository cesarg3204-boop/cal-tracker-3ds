/* Cal Tracker 3DS: a standalone calorie and macro tracker.
 *
 * Top screen: the day (totals against targets, and the food log).
 * Bottom screen: per-meal subtotals, the current list's details and controls.
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

#define DATA_DIR "sdmc:/3ds/caltracker"
#define TOP_W 400
#define BOTTOM_W 320
#define SCREEN_H 240
#define ROW_H 15.0f
#define LIST_ROWS 10
#define TEXT_SCALE 0.5f

/* ---- colours ------------------------------------------------------------ */
static u32 C_BG, C_PANEL, C_TEXT, C_MUTED, C_ACCENT, C_OVER, C_SEL, C_BAR_BG;
static u32 C_PROTEIN, C_CARBS, C_FAT;

static void init_colors(void)
{
    C_BG = C2D_Color32(0x16, 0x1b, 0x22, 0xff);
    C_PANEL = C2D_Color32(0x22, 0x29, 0x33, 0xff);
    C_TEXT = C2D_Color32(0xee, 0xf1, 0xf5, 0xff);
    C_MUTED = C2D_Color32(0x93, 0x9d, 0xab, 0xff);
    C_ACCENT = C2D_Color32(0x4c, 0xc9, 0x7a, 0xff);
    C_OVER = C2D_Color32(0xe8, 0x6a, 0x5c, 0xff);
    C_SEL = C2D_Color32(0x2f, 0x4a, 0x6b, 0xff);
    C_BAR_BG = C2D_Color32(0x33, 0x3b, 0x47, 0xff);
    C_PROTEIN = C2D_Color32(0x5b, 0x9b, 0xf0, 0xff);
    C_CARBS = C2D_Color32(0xf0, 0xb4, 0x4c, 0xff);
    C_FAT = C2D_Color32(0xd0, 0x6c, 0xd8, 0xff);
}

/* ---- app state ---------------------------------------------------------- */
typedef enum { SCR_DAY, SCR_ADD, SCR_RESULTS } Screen;

static DayLog g_day;
static FoodCache g_cache;
static Targets g_targets;
static char g_today[CT_DATE_LEN];

static Screen g_screen = SCR_DAY;
static int g_sel = 0, g_scroll = 0;
static Meal g_meal = MEAL_BREAKFAST;
static Food g_results[OFF_PAGE_SIZE];
static int g_result_count = 0;
static char g_query[64] = "";
static char g_status[96] = "";
static bool g_search_pending = false;
static bool g_net_ok = false;

/* Display order of the day's entries: grouped by meal, stable. */
static size_t g_order[CT_MAX_ENTRIES];

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

static void bar(float x, float y, float w, float h, float value, float target, u32 color)
{
    C2D_DrawRectSolid(x, y, 0.5f, w, h, C_BAR_BG);
    if (target <= 0)
        return;
    float frac = value / target;
    u32 c = frac > 1.0f ? C_OVER : color;
    if (frac > 1.0f)
        frac = 1.0f;
    C2D_DrawRectSolid(x, y, 0.5f, w * frac, h, c);
}

static void status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_status, sizeof g_status, fmt, ap);
    va_end(ap);
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

static void enter_screen(Screen s)
{
    g_screen = s;
    g_sel = 0;
    g_scroll = 0;
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

static void load_day(const char *date)
{
    if (day_load(date, &g_day) != 0)
        status("Could not read the log for %s", date);
    rebuild_order();
    g_sel = 0;
    g_scroll = 0;
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

/* Ask for grams and log the food to the current day. */
static bool log_food(const Food *food)
{
    float grams = food->serving_g > 0 ? food->serving_g : 100.0f;
    if (!prompt_number("Grams eaten", grams, &grams) || grams <= 0)
        return false;
    Entry e;
    e.food = *food;
    e.grams = grams;
    e.meal = g_meal;
    if (day_add(&g_day, &e) != 0) {
        status("This day is full (%d entries)", CT_MAX_ENTRIES);
        return false;
    }
    save_day();
    foods_touch(&g_cache, food);
    foods_save(&g_cache);
    Totals t = entry_totals(&e);
    status("Added %.0f g %.24s (%.0f kcal)", grams, food->name, t.kcal);
    return true;
}

/* Custom food: values are entered per serving, stored per 100 g. */
static void add_custom_food(void)
{
    Food f;
    memset(&f, 0, sizeof f);
    float serving = 100, kcal = 0, p = 0, c = 0, fat = 0;
    if (!prompt_text("Food name", NULL, f.name, sizeof f.name)) return;
    if (!prompt_number("Serving size in grams", serving, &serving) || serving <= 0) return;
    if (!prompt_number("Calories per serving", kcal, &kcal)) return;
    if (!prompt_number("Protein (g) per serving", p, &p)) return;
    if (!prompt_number("Carbs (g) per serving", c, &c)) return;
    if (!prompt_number("Fat (g) per serving", fat, &fat)) return;
    float k = 100.0f / serving;
    f.kcal = kcal * k;
    f.protein = p * k;
    f.carbs = c * k;
    f.fat = fat * k;
    f.serving_g = serving;
    ct_strlcpy(f.brand, "Custom", sizeof f.brand);
    if (log_food(&f))
        enter_screen(SCR_DAY);
}

static void run_search(void)
{
    char url[512];
    char err[96] = "";
    g_result_count = 0;
    /* Try each endpoint in turn; the first one that answers with valid JSON wins. */
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
        if (n > 0)
            status("%d results for \"%.30s\"", n, g_query);
        else
            status("No results for \"%.30s\"", g_query);
        return;
    }
    status("Search failed: %s", err);
}

/* ---- input -------------------------------------------------------------- */
static void move_sel(int delta, int count)
{
    if (count <= 0) {
        g_sel = 0;
        g_scroll = 0;
        return;
    }
    g_sel += delta;
    if (g_sel < 0) g_sel = count - 1;
    if (g_sel >= count) g_sel = 0;
    if (g_sel < g_scroll) g_scroll = g_sel;
    if (g_sel >= g_scroll + LIST_ROWS) g_scroll = g_sel - LIST_ROWS + 1;
}

static void edit_targets(void)
{
    Targets t = g_targets;
    if (!prompt_number("Daily calories", t.kcal, &t.kcal)) return;
    if (!prompt_number("Daily protein (g)", t.protein, &t.protein)) return;
    if (!prompt_number("Daily carbs (g)", t.carbs, &t.carbs)) return;
    if (!prompt_number("Daily fat (g)", t.fat, &t.fat)) return;
    g_targets = t;
    if (targets_save(&g_targets) == 0)
        status("Targets saved");
    else
        status("Could not save targets");
}

/* Returns false to quit. */
static bool update(u32 down)
{
    if (down & KEY_START)
        return false;

    switch (g_screen) {
    case SCR_DAY: {
        int count = (int)g_day.count;
        char date[CT_DATE_LEN];
        if (down & (KEY_DLEFT | KEY_L)) {
            if (date_add_days(g_day.date, -1, date) == 0) load_day(date);
        } else if (down & (KEY_DRIGHT | KEY_R)) {
            if (date_add_days(g_day.date, 1, date) == 0) load_day(date);
        } else if (down & KEY_DUP) {
            move_sel(-1, count);
        } else if (down & KEY_DDOWN) {
            move_sel(1, count);
        } else if (down & KEY_A) {
            /* today: guess the meal from the clock; past days: the selected entry's meal */
            if (strcmp(g_day.date, g_today) == 0)
                g_meal = meal_for_now();
            else
                g_meal = count > 0 ? g_day.entries[g_order[g_sel]].meal : MEAL_BREAKFAST;
            enter_screen(SCR_ADD);
        } else if ((down & KEY_X) && count > 0) {
            Entry *e = &g_day.entries[g_order[g_sel]];
            status("Removed %.30s", e->food.name);
            day_remove(&g_day, g_order[g_sel]);
            save_day();
            move_sel(0, (int)g_day.count);
            if (g_sel >= (int)g_day.count) move_sel(-1, (int)g_day.count);
        } else if ((down & KEY_Y) && count > 0) {
            Entry *e = &g_day.entries[g_order[g_sel]];
            float grams = e->grams;
            if (prompt_number("Grams eaten", grams, &grams) && grams > 0) {
                e->grams = grams;
                save_day();
                status("Updated %.30s", e->food.name);
            }
        } else if (down & KEY_SELECT) {
            edit_targets();
        } else if (down & (KEY_ZL | KEY_ZR | KEY_B)) {
            load_day(g_today);
        }
        break;
    }
    case SCR_ADD: {
        int count = (int)g_cache.count;
        if (down & KEY_B) {
            enter_screen(SCR_DAY);
        } else if (down & (KEY_L | KEY_DLEFT)) {
            g_meal = (Meal)((g_meal + MEAL_COUNT - 1) % MEAL_COUNT);
        } else if (down & (KEY_R | KEY_DRIGHT)) {
            g_meal = (Meal)((g_meal + 1) % MEAL_COUNT);
        } else if (down & KEY_DUP) {
            move_sel(-1, count);
        } else if (down & KEY_DDOWN) {
            move_sel(1, count);
        } else if ((down & KEY_A) && count > 0) {
            Food f = g_cache.items[g_sel];
            if (log_food(&f)) enter_screen(SCR_DAY);
        } else if (down & KEY_Y) {
            if (!g_net_ok) {
                status("No network: online search is off");
            } else if (prompt_text("Search foods (e.g. greek yogurt)", g_query[0] ? g_query : NULL,
                                   g_query, sizeof g_query)) {
                status("Searching for \"%.30s\"...", g_query);
                g_result_count = 0;
                g_search_pending = true;
                enter_screen(SCR_RESULTS);
            }
        } else if (down & KEY_X) {
            add_custom_food();
        }
        break;
    }
    case SCR_RESULTS: {
        if (g_search_pending)
            break;
        if (down & KEY_B) {
            enter_screen(SCR_ADD);
        } else if (down & (KEY_L | KEY_DLEFT)) {
            g_meal = (Meal)((g_meal + MEAL_COUNT - 1) % MEAL_COUNT);
        } else if (down & (KEY_R | KEY_DRIGHT)) {
            g_meal = (Meal)((g_meal + 1) % MEAL_COUNT);
        } else if (down & KEY_DUP) {
            move_sel(-1, g_result_count);
        } else if (down & KEY_DDOWN) {
            move_sel(1, g_result_count);
        } else if ((down & KEY_A) && g_result_count > 0) {
            if (log_food(&g_results[g_sel])) enter_screen(SCR_DAY);
        } else if (down & KEY_Y) {
            if (prompt_text("Search foods", g_query, g_query, sizeof g_query)) {
                status("Searching for \"%.30s\"...", g_query);
                g_result_count = 0;
                g_search_pending = true;
                enter_screen(SCR_RESULTS);
            }
        }
        break;
    }
    }
    return true;
}

/* ---- screens ------------------------------------------------------------ */
static void draw_food_row(float y, bool selected, const Food *f, const char *right)
{
    if (selected)
        C2D_DrawRectSolid(4, y - 1, 0.4f, TOP_W - 8, ROW_H, C_SEL);
    if (f->brand[0])
        text(10, y, TEXT_SCALE, C_TEXT, 0, "%.30s  (%.14s)", f->name, f->brand);
    else
        text(10, y, TEXT_SCALE, C_TEXT, 0, "%.40s", f->name);
    text(TOP_W - 10, y, TEXT_SCALE, C_MUTED, C2D_AlignRight, "%s", right);
}

static void draw_list_hint(float y, int count)
{
    if (count > LIST_ROWS)
        text(TOP_W - 10, y, 0.4f, C_MUTED, C2D_AlignRight, "%d-%d of %d", g_scroll + 1,
             g_scroll + LIST_ROWS < count ? g_scroll + LIST_ROWS : count, count);
}

static void draw_top_day(void)
{
    Totals t = sum_entries(g_day.entries, g_day.count, -1);
    bool is_today = strcmp(g_day.date, g_today) == 0;
    text(10, 6, 0.6f, C_TEXT, 0, "%s%s", g_day.date, is_today ? "  (today)" : "");
    text(TOP_W - 10, 6, 0.6f, t.kcal > g_targets.kcal ? C_OVER : C_ACCENT, C2D_AlignRight,
         "%.0f / %.0f kcal", t.kcal, g_targets.kcal);
    bar(10, 28, TOP_W - 20, 8, t.kcal, g_targets.kcal, C_ACCENT);

    float col = (TOP_W - 20) / 3.0f;
    text(10, 40, 0.45f, C_TEXT, 0, "Protein %.0f/%.0f g", t.protein, g_targets.protein);
    text(10 + col, 40, 0.45f, C_TEXT, 0, "Carbs %.0f/%.0f g", t.carbs, g_targets.carbs);
    text(10 + 2 * col, 40, 0.45f, C_TEXT, 0, "Fat %.0f/%.0f g", t.fat, g_targets.fat);
    bar(10, 56, col - 8, 4, t.protein, g_targets.protein, C_PROTEIN);
    bar(10 + col, 56, col - 8, 4, t.carbs, g_targets.carbs, C_CARBS);
    bar(10 + 2 * col, 56, col - 8, 4, t.fat, g_targets.fat, C_FAT);

    float y = 70;
    if (g_day.count == 0) {
        text(10, y + 20, TEXT_SCALE, C_MUTED, 0, "Nothing logged yet. Press A to add food.");
        return;
    }
    for (int row = 0; row < LIST_ROWS && g_scroll + row < (int)g_day.count; row++) {
        int idx = g_scroll + row;
        const Entry *e = &g_day.entries[g_order[idx]];
        Totals et = entry_totals(e);
        char right[40];
        snprintf(right, sizeof right, "%c  %.0f g  %.0f kcal", meal_name(e->meal)[0], e->grams, et.kcal);
        draw_food_row(y + row * ROW_H, idx == g_sel, &e->food, right);
    }
    draw_list_hint(y + LIST_ROWS * ROW_H + 2, (int)g_day.count);
}

static void draw_top_foods(const char *title, const Food *foods, int count, const char *empty)
{
    text(10, 6, 0.6f, C_TEXT, 0, "%s", title);
    text(TOP_W - 10, 6, 0.6f, C_ACCENT, C2D_AlignRight, "< %s >", meal_name(g_meal));
    float y = 34;
    if (g_search_pending) {
        text(10, y + 20, TEXT_SCALE, C_MUTED, 0, "Searching Open Food Facts...");
        return;
    }
    if (count == 0) {
        text(10, y + 20, TEXT_SCALE, C_MUTED, 0, "%s", empty);
        return;
    }
    for (int row = 0; row < LIST_ROWS + 2 && g_scroll + row < count; row++) {
        int idx = g_scroll + row;
        char right[24];
        snprintf(right, sizeof right, "%.0f kcal/100g", foods[idx].kcal);
        draw_food_row(y + row * ROW_H, idx == g_sel, &foods[idx], right);
    }
    draw_list_hint(y + (LIST_ROWS + 2) * ROW_H + 2, count);
}

static void draw_bottom(void)
{
    float y = 8;
    if (g_screen == SCR_DAY) {
        for (int m = 0; m < MEAL_COUNT; m++) {
            Totals mt = sum_entries(g_day.entries, g_day.count, m);
            text(10, y, TEXT_SCALE, C_TEXT, 0, "%s", meal_name((Meal)m));
            text(BOTTOM_W - 10, y, TEXT_SCALE, C_MUTED, C2D_AlignRight, "%.0f kcal  P%.0f C%.0f F%.0f",
                 mt.kcal, mt.protein, mt.carbs, mt.fat);
            y += ROW_H;
        }
        y += 6;
        if (g_day.count > 0) {
            const Entry *e = &g_day.entries[g_order[g_sel]];
            Totals et = entry_totals(e);
            C2D_DrawRectSolid(6, y - 2, 0.4f, BOTTOM_W - 12, 34, C_PANEL);
            text(10, y, 0.45f, C_TEXT, 0, "%.44s", e->food.name);
            text(10, y + 15, 0.45f, C_MUTED, 0, "%.0f g, %.0f kcal, P %.1f  C %.1f  F %.1f",
                 e->grams, et.kcal, et.protein, et.carbs, et.fat);
        }
        y = 150;
        text(10, y, 0.42f, C_MUTED, 0, "A add food    X remove    Y change grams");
        text(10, y + 14, 0.42f, C_MUTED, 0, "Left/Right change day    B back to today");
        text(10, y + 28, 0.42f, C_MUTED, 0, "SELECT daily targets    START quit");
    } else {
        const Food *f = NULL;
        if (g_screen == SCR_ADD && g_cache.count > 0)
            f = &g_cache.items[g_sel];
        if (g_screen == SCR_RESULTS && g_result_count > 0 && !g_search_pending)
            f = &g_results[g_sel];
        if (f) {
            C2D_DrawRectSolid(6, y - 2, 0.4f, BOTTOM_W - 12, 64, C_PANEL);
            text(10, y, 0.5f, C_TEXT, 0, "%.40s", f->name);
            if (f->brand[0]) text(10, y + 15, 0.45f, C_MUTED, 0, "%.40s", f->brand);
            text(10, y + 31, 0.45f, C_TEXT, 0, "Per 100 g: %.0f kcal", f->kcal);
            text(10, y + 45, 0.45f, C_MUTED, 0, "Protein %.1f g   Carbs %.1f g   Fat %.1f g", f->protein,
                 f->carbs, f->fat);
        }
        y = 150;
        text(10, y, 0.42f, C_MUTED, 0, "A log this food    L/R change meal");
        if (g_screen == SCR_ADD)
            text(10, y + 14, 0.42f, C_MUTED, 0, "Y search online    X custom food");
        else
            text(10, y + 14, 0.42f, C_MUTED, 0, "Y new search");
        text(10, y + 28, 0.42f, C_MUTED, 0, "B back");
    }
    if (g_status[0])
        text(10, SCREEN_H - 18, 0.42f, C_ACCENT, 0, "%.60s", g_status);
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
    date_today(g_today);
    load_day(g_today);
    if (!g_net_ok)
        status("No network: only saved foods available");

    while (aptMainLoop()) {
        hidScanInput();
        if (!update(hidKeysDown()))
            break;

        C2D_TextBufClear(g_textbuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, C_BG);
        C2D_SceneBegin(top);
        if (g_screen == SCR_DAY)
            draw_top_day();
        else if (g_screen == SCR_ADD)
            draw_top_foods("Add food: saved foods", g_cache.items, (int)g_cache.count,
                           "No saved foods yet. Y to search, X for a custom food.");
        else
            draw_top_foods("Search results", g_results, g_result_count, "No results. Y to search again.");
        C2D_TargetClear(bottom, C_BG);
        C2D_SceneBegin(bottom);
        draw_bottom();
        C3D_FrameEnd(0);

        /* Run the search after a frame that says "Searching..." has been shown. */
        if (g_search_pending) {
            run_search();
            g_search_pending = false;
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
