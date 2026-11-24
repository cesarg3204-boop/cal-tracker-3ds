#include "barcode.h"

#include <stdlib.h>
#include <string.h>

#define MAX_RUNS 400

/* Widths of the four parts of each digit, in modules. "L" digits start with a
 * space, "R" digits with a bar and use the same widths. "G" is L reversed. */
static const unsigned char L_CODE[10][4] = {
    {3, 2, 1, 1}, {2, 2, 2, 1}, {2, 1, 2, 2}, {1, 4, 1, 1}, {1, 1, 3, 2},
    {1, 2, 3, 1}, {1, 1, 1, 4}, {1, 3, 1, 2}, {1, 2, 1, 3}, {3, 1, 1, 2},
};
/* Which left digits use G instead of L gives the first EAN-13 digit
 * (bit 5 = first left digit, 1 = G). */
static const unsigned char FIRST_DIGIT[10] = {0x00, 0x0B, 0x0D, 0x0E, 0x13, 0x19, 0x1C, 0x15, 0x16, 0x1A};

int barcode_check_ok(const char *d)
{
    size_t n = strlen(d);
    if (n != 13 && n != 8)
        return 0;
    int sum = 0;
    /* weights 3,1,3,1... counted from the digit left of the check digit */
    for (size_t i = 0; i + 1 < n; i++) {
        int w = ((n - 1 - i) % 2 == 1) ? 3 : 1;
        sum += (d[i] - '0') * w;
    }
    return (10 - sum % 10) % 10 == d[n - 1] - '0';
}

/* Best digit for 4 run widths. `reversed` reads them as a G code. Returns the
 * digit, or -1 if nothing is close. *err gets the mismatch. */
static int match_digit(const int *w, int reversed, float *err)
{
    float total = (float)(w[0] + w[1] + w[2] + w[3]);
    if (total <= 0)
        return -1;
    float best = 1e9f;
    int digit = -1;
    for (int d = 0; d < 10; d++) {
        float e = 0;
        for (int k = 0; k < 4; k++) {
            float want = L_CODE[d][reversed ? 3 - k : k] / 7.0f;
            float diff = w[k] / total - want;
            e += diff * diff;
        }
        if (e < best) {
            best = e;
            digit = d;
        }
    }
    *err = best;
    /* one module off in one part is about (1/7)^2 * 2 = 0.04 */
    return best < 0.012f ? digit : -1;
}

static int similar(float a, float b)
{
    return a < b * 1.8f && b < a * 1.8f;
}

/* Guard bars: n runs that are each about one module wide. */
static int guard_ok(const int *runs, int n, float module)
{
    for (int i = 0; i < n; i++)
        if (!similar((float)runs[i], module))
            return 0;
    return 1;
}

/* One digit of 4 runs: tries L and (if allowed) G widths. Returns the digit
 * and sets *is_g, or -1. */
static int read_digit(const int *runs, int allow_l, int allow_g, int *is_g)
{
    float eL = 1, eG = 1;
    int dL = allow_l ? match_digit(runs, 0, &eL) : -1;
    int dG = allow_g ? match_digit(runs, 1, &eG) : -1;
    if (dL < 0 && dG < 0)
        return -1;
    *is_g = dG >= 0 && (dL < 0 || eG < eL);
    return *is_g ? dG : dL;
}

/* Tries to read a barcode whose start guard begins at runs[s] (a bar).
 * half is 6 (EAN-13) or 4 (EAN-8). */
static int decode_at(const int *runs, int count, int s, int half, char out[BARCODE_LEN])
{
    int need = 3 + half * 4 + 5 + half * 4 + 3;
    if (s + need > count)
        return 0;
    float module = (runs[s] + runs[s + 1] + runs[s + 2]) / 3.0f;
    if (module < 1.0f || !guard_ok(runs + s, 3, module))
        return 0;
    /* quiet zone: the space before the start guard should be wide */
    if (s > 0 && runs[s - 1] < module * 3)
        return 0;

    char left[6], right[6];
    int parity = 0; /* bit (half-1-k) set when left digit k is a G digit */
    int i = s + 3;
    for (int k = 0; k < half; k++, i += 4) {
        int is_g;
        int d = read_digit(runs + i, 1, half == 6, &is_g);
        if (d < 0)
            return 0;
        left[k] = (char)('0' + d);
        if (is_g)
            parity |= 1 << (half - 1 - k);
    }
    float mid = 0;
    for (int k = 0; k < 5; k++)
        mid += runs[i + k];
    if (!guard_ok(runs + i, 5, mid / 5.0f) || !similar(mid / 5.0f, module))
        return 0;
    i += 5;
    for (int k = 0; k < half; k++, i += 4) {
        int is_g;
        int d = read_digit(runs + i, 1, 0, &is_g);
        if (d < 0)
            return 0;
        right[k] = (char)('0' + d);
    }
    if (!guard_ok(runs + i, 3, module))
        return 0;

    if (half == 4) {
        memcpy(out, left, 4);
        memcpy(out + 4, right, 4);
        out[8] = '\0';
    } else {
        int first = -1;
        for (int f = 0; f < 10; f++)
            if (FIRST_DIGIT[f] == parity)
                first = f;
        if (first < 0)
            return 0;
        out[0] = (char)('0' + first);
        memcpy(out + 1, left, 6);
        memcpy(out + 7, right, 6);
        out[13] = '\0';
    }
    return barcode_check_ok(out);
}

static int decode_runs(const int *runs, int count, int first_is_bar, char out[BARCODE_LEN])
{
    /* bars are at even indexes when the first run is a bar */
    for (int s = first_is_bar ? 0 : 1; s < count; s += 2) {
        if (decode_at(runs, count, s, 6, out) || decode_at(runs, count, s, 4, out))
            return 1;
    }
    return 0;
}

int barcode_read_row(const unsigned char *row, int width, char out[BARCODE_LEN])
{
    if (width < 60)
        return 0;
    /* threshold halfway between the local darkest and brightest pixel, so
     * uneven light and blur are fine */
    enum { WIN = 20 };
    static int runs[MAX_RUNS], rev[MAX_RUNS];
    int count = 0, len = 0, dark = -1, first_dark = 0;
    int lo = 255, hi = 0;
    for (int x = 0; x < width; x++) {
        if (row[x] < lo) lo = row[x];
        if (row[x] > hi) hi = row[x];
    }
    if (hi - lo < 40)
        return 0; /* no contrast: nothing to read */
    for (int x = 0; x < width; x++) {
        int a = x - WIN < 0 ? 0 : x - WIN, b = x + WIN >= width ? width - 1 : x + WIN;
        int lmin = 255, lmax = 0;
        for (int k = a; k <= b; k++) {
            if (row[k] < lmin) lmin = row[k];
            if (row[k] > lmax) lmax = row[k];
        }
        /* a flat area (like the white margin) is never dark */
        int d = (lmax - lmin) * 3 >= (hi - lo) && row[x] < (lmin + lmax) / 2;
        if (d == dark) {
            len++;
        } else {
            if (dark >= 0) {
                if (count >= MAX_RUNS)
                    return 0;
                runs[count++] = len;
            } else {
                first_dark = d;
            }
            dark = d;
            len = 1;
        }
    }
    if (count < MAX_RUNS)
        runs[count++] = len;

    if (decode_runs(runs, count, first_dark, out))
        return 1;
    /* upside down: the same code read from the other end */
    for (int i = 0; i < count; i++)
        rev[i] = runs[count - 1 - i];
    int last_dark = (count % 2 == 1) ? first_dark : !first_dark;
    return decode_runs(rev, count, last_dark, out);
}

int barcode_read_image(const unsigned char *gray, int width, int height, int stride, char out[BARCODE_LEN])
{
    char found[BARCODE_LEN] = "";
    int mid = height / 2;
    for (int k = 0; k < 24; k++) {
        /* middle first, then alternately above and below */
        int off = ((k + 1) / 2) * 6 * (k % 2 ? 1 : -1);
        int y = mid + off;
        if (y < 0 || y >= height)
            continue;
        char code[BARCODE_LEN];
        if (!barcode_read_row(gray + (size_t)y * stride, width, code))
            continue;
        if (found[0] && strcmp(found, code) == 0) {
            memcpy(out, code, BARCODE_LEN);
            return 1;
        }
        memcpy(found, code, BARCODE_LEN);
    }
    return 0;
}
