#include "sound.h"

#include <3ds.h>
#include <math.h>
#include <string.h>

#define RATE 22050
#define CHANNELS 4

static bool g_ok = false, g_on = true;
static s16 *g_data[SND_COUNT];
static u32 g_len[SND_COUNT];
static ndspWaveBuf g_wave[CHANNELS];

#ifndef M_PI
#define M_PI 3.14159265358979f
#endif

/* Adds a soft tone: frequency glides from f0 to f1, with a quick fade in
 * and an exponential fade out. */
static void tone(float *out, u32 len, float start, float dur, float f0, float f1, float vol, float decay)
{
    u32 s0 = (u32)(start * RATE), n = (u32)(dur * RATE);
    float phase = 0;
    for (u32 i = 0; i < n && s0 + i < len; i++) {
        float t = (float)i / RATE;
        float f = f0 + (f1 - f0) * (t / dur);
        phase += 2 * (float)M_PI * f / RATE;
        float env = fminf(1.0f, t * 300.0f) * expf(-t * decay);
        float v = sinf(phase) + 0.15f * sinf(2 * phase);
        out[s0 + i] += v * env * vol;
    }
}

static void make(Sound s)
{
    static float buf[RATE]; /* up to 1 s */
    float dur = 0.0f;
    memset(buf, 0, sizeof buf);
    switch (s) {
    case SND_TAP: /* a soft tick */
        dur = 0.05f;
        tone(buf, RATE, 0, dur, 1500, 1300, 0.22f, 70);
        break;
    case SND_POP: /* food logged */
        dur = 0.14f;
        tone(buf, RATE, 0, dur, 760, 360, 0.55f, 28);
        break;
    case SND_WATER: /* a rising bloop */
        dur = 0.18f;
        tone(buf, RATE, 0, dur, 320, 960, 0.45f, 18);
        break;
    case SND_GOAL: /* a bright two-note chime */
        dur = 0.7f;
        tone(buf, RATE, 0.00f, 0.5f, 1046.5f, 1046.5f, 0.35f, 7);
        tone(buf, RATE, 0.12f, 0.58f, 1568.0f, 1568.0f, 0.35f, 6);
        break;
    case SND_SCAN: /* barcode found */
        dur = 0.16f;
        tone(buf, RATE, 0.00f, 0.07f, 1760, 1760, 0.3f, 10);
        tone(buf, RATE, 0.08f, 0.08f, 2349, 2349, 0.3f, 10);
        break;
    default:
        return;
    }
    u32 len = (u32)(dur * RATE);
    g_data[s] = linearAlloc(len * sizeof(s16));
    if (!g_data[s])
        return;
    for (u32 i = 0; i < len; i++) {
        float v = buf[i];
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        g_data[s][i] = (s16)(v * 32000);
    }
    g_len[s] = len;
    DSP_FlushDataCache(g_data[s], len * sizeof(s16));
}

int sound_init(void)
{
    if (R_FAILED(ndspInit()))
        return -1;
    g_ok = true;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int ch = 0; ch < CHANNELS; ch++) {
        ndspChnReset(ch);
        ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
        ndspChnSetRate(ch, RATE);
        ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
        float mix[12];
        memset(mix, 0, sizeof mix);
        mix[0] = mix[1] = 0.9f;
        ndspChnSetMix(ch, mix);
    }
    for (int s = 0; s < SND_COUNT; s++)
        make((Sound)s);
    return 0;
}

void sound_exit(void)
{
    if (!g_ok)
        return;
    for (int ch = 0; ch < CHANNELS; ch++)
        ndspChnWaveBufClear(ch);
    ndspExit();
    for (int s = 0; s < SND_COUNT; s++)
        if (g_data[s])
            linearFree(g_data[s]);
    g_ok = false;
}

void sound_enable(bool on)
{
    g_on = on;
}

void sound_play(Sound s)
{
    if (!g_ok || !g_on || s < 0 || s >= SND_COUNT || !g_data[s])
        return;
    static int next = 0;
    int ch = next;
    next = (next + 1) % CHANNELS;
    ndspChnWaveBufClear(ch);
    memset(&g_wave[ch], 0, sizeof g_wave[ch]);
    g_wave[ch].data_vaddr = g_data[s];
    g_wave[ch].nsamples = g_len[s];
    g_wave[ch].looping = false;
    ndspChnWaveBufAdd(ch, &g_wave[ch]);
}
