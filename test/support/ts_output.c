#include "ts_output.h"

#include <stdio.h>
#include <string.h>

static int ts_unity_quiet;
static char ts_saved_abort_frame[sizeof(Unity.AbortFrame)];

void ts_unity_putchar(int c)
{
    if (!ts_unity_quiet) {
        (void)putchar(c);
    }
}

void ts_fail_trap_enter(void)
{
    /* TEST_PROTECT() overwrites the abort frame of the running test; keep it for restoring. */
    memcpy(ts_saved_abort_frame, &Unity.AbortFrame, sizeof(ts_saved_abort_frame));
    Unity.CurrentTestFailed = 0;
    ts_unity_quiet = 1;
}

void ts_fail_trap_leave(unsigned line)
{
    int failed = (int)Unity.CurrentTestFailed;

    ts_unity_quiet = 0;
    memcpy(&Unity.AbortFrame, ts_saved_abort_frame, sizeof(ts_saved_abort_frame));
    Unity.CurrentTestFailed = 0;
    if (!failed) {
        UnityFail("expected the checked statement to fail the test", (UNITY_LINE_TYPE)line);
    }
}
