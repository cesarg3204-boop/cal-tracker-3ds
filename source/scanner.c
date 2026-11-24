#include "scanner.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#define CAM_W 400
#define CAM_H 240
#define TEX_W 512
#define TEX_H 256

static bool g_on = false;
static u16 *g_frame;           /* camera picture, RGB565, row by row */
static unsigned char *g_gray;  /* the same picture in gray */
static C3D_Tex g_tex;
static Tex3DS_SubTexture g_sub = {CAM_W, CAM_H, 0.0f, 1.0f, CAM_W / (float)TEX_W, 1.0f - CAM_H / (float)TEX_H};
static u32 g_transfer;
static Handle g_recv_event = 0, g_error_event = 0;

bool scanner_running(void)
{
    return g_on;
}

int scanner_start(void)
{
    if (g_on)
        return 0;
    g_frame = malloc(CAM_W * CAM_H * sizeof(u16));
    g_gray = malloc(CAM_W * CAM_H);
    if (!g_frame || !g_gray || !C3D_TexInit(&g_tex, TEX_W, TEX_H, GPU_RGB565)) {
        free(g_frame);
        free(g_gray);
        g_frame = NULL;
        g_gray = NULL;
        return -1;
    }
    memset(g_tex.data, 0, g_tex.size);
    C3D_TexSetFilter(&g_tex, GPU_LINEAR, GPU_LINEAR);
    if (R_FAILED(camInit())) {
        C3D_TexDelete(&g_tex);
        free(g_frame);
        free(g_gray);
        g_frame = NULL;
        g_gray = NULL;
        return -1;
    }
    CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
    CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_15);
    CAMU_SetNoiseFilter(SELECT_OUT1, true);
    CAMU_SetAutoExposure(SELECT_OUT1, true);
    CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
    CAMU_SetTrimming(PORT_CAM1, false);
    CAMU_GetMaxBytes(&g_transfer, CAM_W, CAM_H);
    CAMU_SetTransferBytes(PORT_CAM1, g_transfer, CAM_W, CAM_H);
    CAMU_GetBufferErrorInterruptEvent(&g_error_event, PORT_CAM1);
    CAMU_Activate(SELECT_OUT1);
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_StartCapture(PORT_CAM1);
    g_recv_event = 0;
    g_on = true;
    return 0;
}

void scanner_stop(void)
{
    if (!g_on)
        return;
    CAMU_StopCapture(PORT_CAM1);
    if (g_recv_event)
        svcCloseHandle(g_recv_event);
    g_recv_event = 0;
    if (g_error_event)
        svcCloseHandle(g_error_event);
    g_error_event = 0;
    CAMU_Activate(SELECT_NONE);
    camExit();
    C3D_TexDelete(&g_tex);
    free(g_frame);
    free(g_gray);
    g_frame = NULL;
    g_gray = NULL;
    g_on = false;
}

/* Position of texel (x, y) in a 3DS tiled texture: 8x8 tiles, Morton order
 * inside each tile. */
static inline u32 tiled(u32 x, u32 y)
{
    u32 tile = ((y >> 3) * (TEX_W >> 3) + (x >> 3)) << 6;
    u32 in = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    return tile + in;
}

int scanner_frame(char out[BARCODE_LEN])
{
    if (!g_on)
        return 0;
    /* Ask for the next picture once, then check without blocking. If the
     * camera's buffer overflows (we were busy), it stops until restarted. */
    if (g_recv_event == 0)
        CAMU_SetReceiving(&g_recv_event, g_frame, PORT_CAM1, CAM_W * CAM_H * sizeof(u16), (s16)g_transfer);
    if (svcWaitSynchronization(g_error_event, 0) == 0) {
        svcCloseHandle(g_recv_event);
        g_recv_event = 0;
        CAMU_ClearBuffer(PORT_CAM1);
        CAMU_StartCapture(PORT_CAM1);
        return 0;
    }
    if (svcWaitSynchronization(g_recv_event, 0) != 0)
        return 0; /* not ready yet: keep the last picture */
    svcCloseHandle(g_recv_event);
    g_recv_event = 0;

    u16 *tex = (u16 *)g_tex.data;
    for (u32 y = 0; y < CAM_H; y++) {
        const u16 *src = g_frame + y * CAM_W;
        unsigned char *g = g_gray + y * CAM_W;
        for (u32 x = 0; x < CAM_W; x++) {
            u16 p = src[x];
            u32 r = (p >> 11) & 31, gr = (p >> 5) & 63, b = p & 31;
            g[x] = (unsigned char)((r * 8 * 77 + gr * 4 * 150 + b * 8 * 29) >> 8);
            tex[tiled(x, y)] = p;
        }
    }
    C3D_TexFlush(&g_tex);
    /* ask for the next picture right away, while we decode this one */
    CAMU_SetReceiving(&g_recv_event, g_frame, PORT_CAM1, CAM_W * CAM_H * sizeof(u16), (s16)g_transfer);
    return barcode_read_image(g_gray, CAM_W, CAM_H, CAM_W, out);
}

C2D_Image scanner_image(void)
{
    C2D_Image img = {&g_tex, &g_sub};
    return img;
}
