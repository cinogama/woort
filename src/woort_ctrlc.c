/*
 * woort_ctrlc.c - Ctrl+C signal handling implementation.
 *
 * Registers a SIGINT handler.  On the first SIGINT the WAIPO debugger is
 * attached and every root VM receives a debug-callback request.
 * Consecutive SIGINT within a 2-second window are counted; after 4 hits the
 * process logs a message and calls abort().
 */

#include "woort.h"
#include "woort_log.h"

#include <signal.h>
#include <stdlib.h>
#include <time.h>

/* --------------------------------------------------------------- */

static void _woort_ctrlc_signal_handler(int sig);

/* --------------------------------------------------------------- */

static void _woort_ctrlc_signal_handler(int sig)
{
    (void)sig;

    woort_log(
        "CTRL+C: Trying to breakdown all virtual-machine by default debuggee "
        "immediately.\n");

    switch (woort_WAIPO_Debugger_attach())
    {
    case WOORT_DEBUGGER_ATTACH_RESULT_SUCCESS:
    case WOORT_DEBUGGER_ATTACH_RESULT_ALREADY_ATTACHED:
        break;
    case WOORT_DEBUGGER_ATTACH_RESULT_FAILED:
        woort_log("CTRL+C: Failed to attach debugger (out of memory).\n");
        break;
    }
    woort_VMRuntime_Debugger_breakdown_all_vm();

    /*
     * Track consecutive Ctrl+C presses within a 2-second window.
     * After 4 hits in that window the process is force-aborted.
     */
    static size_t hit_count    = 0;
    static time_t last_hit_sec = 0;

    const time_t now_sec = time(NULL);

    if (now_sec - last_hit_sec < 2)
    {
        if (hit_count >= 4)
        {
            woort_log("CTRL+C: Panic termination.\n");
            abort();
        }
        else
        {
            woort_log(
                "CTRL+C: Continue pressing Ctrl+C %zu time(s) to trigger a "
                "panic termination.\n",
                4 - hit_count);
        }
    }
    else
    {
        hit_count = 0;
    }

    last_hit_sec = now_sec;
    ++hit_count;

    /* Re-register ourselves (traditional Unix signal semantics). */
    signal(SIGINT, _woort_ctrlc_signal_handler);
}

/* --------------------------------------------------------------- */

void woort_ctrlc_setup(void)
{
    signal(SIGINT, _woort_ctrlc_signal_handler);
}

void woort_ctrlc_teardown(void)
{
    signal(SIGINT, SIG_DFL);
}
