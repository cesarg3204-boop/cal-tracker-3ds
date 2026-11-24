#include "pedometer.h"

#include <3ds.h>
#include <string.h>
#include <time.h>

#include "core/activity.h"
#include "core/date.h"

static bool g_ok = false;

int pedometer_init(void)
{
    g_ok = R_SUCCEEDED(ptmuInit());
    return g_ok ? 0 : -1;
}

void pedometer_exit(void)
{
    if (g_ok)
        ptmuExit();
    g_ok = false;
}

bool pedometer_available(void)
{
    return g_ok;
}

/* PTM GetStepHistory (0x000B00C2): `hours` hourly counts starting at
 * `start_ms`. libctru's PTMU_GetStepHistory always starts at "now", so the
 * request is built here with our own start time. */
static Result step_history(u32 hours, u64 start_ms, u16 *out)
{
    u32 *cmd = getThreadCommandBuffer();
    cmd[0] = IPC_MakeHeader(0xB, 3, 2);
    cmd[1] = hours;
    cmd[2] = (u32)(start_ms & 0xFFFFFFFFu);
    cmd[3] = (u32)(start_ms >> 32);
    cmd[4] = IPC_Desc_Buffer(hours * sizeof(u16), IPC_BUFFER_W);
    cmd[5] = (u32)(uintptr_t)out;
    Result rc = svcSendSyncRequest(*ptmuGetSessionHandle());
    return R_FAILED(rc) ? rc : (Result)cmd[1];
}

int pedometer_day(const char *date, uint16_t hours[24], unsigned *total)
{
    memset(hours, 0, 24 * sizeof hours[0]);
    *total = 0;
    if (!g_ok)
        return -1;

    char today[CT_DATE_LEN];
    date_today(today);
    int cmp = strcmp(date, today);
    if (cmp > 0)
        return 0; /* the future has no steps */
    u32 count = 24;
    if (cmp == 0) {
        /* the 3DS clock has no time zone: time() is the console's local time */
        time_t now = time(NULL);
        count = (u32)((now % 86400) / 3600) + 1;
    }
    long long start;
    if (date_pedometer_ms(date, &start) != 0)
        return -1;
    static u16 buf[24];
    memset(buf, 0, sizeof buf);
    if (R_FAILED(step_history(count, (u64)start, buf)))
        return -1;
    for (u32 i = 0; i < count; i++) {
        hours[i] = buf[i];
        *total += buf[i];
    }
    return 0;
}
