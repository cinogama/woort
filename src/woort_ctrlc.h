/*
 * woort_ctrlc.h - Ctrl+C signal handling for debugger activation.
 *
 * Registers a SIGINT handler that:
 *   1st press -> Attaches debugger and breaks down all VMs.
 *   Consecutive presses within 2s -> Counts toward panic threshold (4 hits).
 *   4th consecutive press -> Logs and aborts the process.
 *
 * Used by the host (woolang driver) to wire Ctrl+C via:
 *   woort_ctrlc_setup()   at program startup
 *   woort_ctrlc_teardown() at program shutdown
 */
#pragma once

/**
 * @brief Register the Ctrl+C signal handler.
 *
 * On SIGINT the handler attaches the WAIPO debugger and breaks down all VMs.
 * Consecutive SIGINT within 2 seconds are counted; after 4 hits the process
 * is forcefully aborted via abort().
 */
void woort_ctrlc_setup(void);

/**
 * @brief Restore the default SIGINT disposition.
 */
void woort_ctrlc_teardown(void);
