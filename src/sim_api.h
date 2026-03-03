/*
 * sim_api.h — Public C API for the sim6502 simulator core.
 *
 * The simulator session is an opaque handle (sim_session_t *).  All mutable
 * state lives inside the session; the caller never touches internals.
 *
 * Thread-safety: Phase 1 is single-threaded.  The GUI drives execution by
 * calling sim_step() each frame.  Phase 2 will add a mutex-guarded background
 * thread; the API surface is the same.
 */

#ifndef SIM_API_H
#define SIM_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "cpu.h"

/* --------------------------------------------------------------------------
 * Execution state machine
 * -------------------------------------------------------------------------- */
typedef enum {
    SIM_IDLE,      /* no program loaded                                    */
    SIM_READY,     /* program loaded, not yet stepped                      */
    SIM_PAUSED,    /* stepped at least once; stopped (breakpoint/manual)   */
    SIM_FINISHED,  /* reached BRK, STP, or cycle limit                    */
} sim_state_t;

/* Event codes returned by sim_step() and passed to sim_event_cb */
#define SIM_EVENT_BREAK  1   /* execution stopped at a breakpoint            */
#define SIM_EVENT_BRK    2   /* BRK instruction reached                      */
#define SIM_EVENT_STP    3   /* STP instruction reached                      */
#define SIM_EVENT_STEP   4   /* single step completed (informational)        */

/* --------------------------------------------------------------------------
 * Opaque session handle
 * -------------------------------------------------------------------------- */
typedef struct sim_session sim_session_t;

/* Event callback: fired on stop conditions */
typedef void (*sim_event_cb)(sim_session_t *s, int event, void *userdata);

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

/* Create a new session.  processor is "6502", "6502-undoc", "65c02",
 * "65ce02", or "45gs02" (NULL defaults to "6502").
 * Returns NULL on allocation failure.                                       */
sim_session_t *sim_create(const char *processor);

/* Destroy the session and free all memory. */
void sim_destroy(sim_session_t *s);

/* --------------------------------------------------------------------------
 * Program loading
 * -------------------------------------------------------------------------- */

/* Assemble 'path' (two-pass) and load it into the session.
 * Returns 0 on success, -1 on error (file not found, assemble failure).
 * On success, state transitions to SIM_READY.                              */
int sim_load_asm(sim_session_t *s, const char *path);

/* --------------------------------------------------------------------------
 * Execution control
 * -------------------------------------------------------------------------- */

/* Run up to 'count' instructions.
 * Returns 0 if all 'count' instructions ran without a stop condition.
 * Returns SIM_EVENT_* (> 0) if a stop condition was hit before count.
 * Returns -1 if state is SIM_IDLE or SIM_FINISHED (call sim_reset first).
 * On stop: state is set to SIM_PAUSED (breakpoint) or SIM_FINISHED (BRK/STP). */
int sim_step(sim_session_t *s, int count);

/* Reset CPU registers to initial state; keep loaded program in memory.
 * State transitions back to SIM_READY.                                     */
void sim_reset(sim_session_t *s);

/* --------------------------------------------------------------------------
 * Disassembler
 * -------------------------------------------------------------------------- */

/* Disassemble one instruction at 'addr' into buf.
 * Returns the number of bytes consumed (>= 1).                             */
int sim_disassemble_one(sim_session_t *s, uint16_t addr, char *buf, size_t len);

/* --------------------------------------------------------------------------
 * State inspection
 * -------------------------------------------------------------------------- */

/* Direct pointer to the session's cpu_t.  Valid until sim_destroy().
 * Read-only from the GUI; modify only through the API (future sim_set_reg). */
cpu_t *sim_get_cpu(sim_session_t *s);

/* Read / write a byte in the session's virtual address space. */
uint8_t sim_mem_read_byte(sim_session_t *s, uint16_t addr);
void    sim_mem_write_byte(sim_session_t *s, uint16_t addr, uint8_t val);

/* --------------------------------------------------------------------------
 * Session metadata
 * -------------------------------------------------------------------------- */

sim_state_t  sim_get_state(sim_session_t *s);
const char  *sim_get_filename(sim_session_t *s);
const char  *sim_processor_name(sim_session_t *s);
const char  *sim_state_name(sim_state_t state);

/* Change the active processor.  Safe to call before or after load.
 * If a program is already loaded, rebuild the dispatch table.              */
void sim_set_processor(sim_session_t *s, const char *name);

/* Return the raw cpu_type_t for the current processor. */
cpu_type_t sim_get_cpu_type(sim_session_t *s);

/* --------------------------------------------------------------------------
 * Event callbacks
 * -------------------------------------------------------------------------- */
void sim_set_event_callback(sim_session_t *s, sim_event_cb cb, void *userdata);

/* --------------------------------------------------------------------------
 * Breakpoints
 * -------------------------------------------------------------------------- */

/* Add a breakpoint at addr with optional condition string.
 * Returns 1 on success, 0 if the breakpoint table is full.                 */
int  sim_break_set(sim_session_t *s, uint16_t addr, const char *cond);

/* Remove the breakpoint at addr (no-op if not found). */
void sim_break_clear(sim_session_t *s, uint16_t addr);

/* --------------------------------------------------------------------------
 * Symbol table
 * -------------------------------------------------------------------------- */

/* Return the name of the symbol at addr, or NULL if none found. */
const char *sim_sym_by_addr(sim_session_t *s, uint16_t addr);

#ifdef __cplusplus
}
#endif

#endif /* SIM_API_H */
