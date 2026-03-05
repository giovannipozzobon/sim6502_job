#ifndef DEBUG_H
#define DEBUG_H

#include "cpu.h"
#include "opcodes.h"

#define MAX_BREAKPOINTS 32

typedef struct {
	breakpoint_t breakpoints[MAX_BREAKPOINTS];
	int num_breakpoints;
	int trace_enabled;
	int step_mode;
	int step_waiting;
	FILE *trace_file;
} debugger_t;

/* Initialize debugger */
static inline void debugger_init(debugger_t *dbg) {
	dbg->num_breakpoints = 0;
	dbg->trace_enabled = 0;
	dbg->step_mode = 0;
	dbg->step_waiting = 0;
	dbg->trace_file = NULL;
	
	for (int i = 0; i < MAX_BREAKPOINTS; i++) {
		dbg->breakpoints[i].enabled = 0;
		dbg->breakpoints[i].address = 0;
	}
}

/* Add breakpoint */
static inline int debugger_add_breakpoint(debugger_t *dbg, unsigned short addr) {
	if (dbg->num_breakpoints >= MAX_BREAKPOINTS) {
		return 0;  /* Breakpoint table full */
	}
	
	dbg->breakpoints[dbg->num_breakpoints].address = addr;
	dbg->breakpoints[dbg->num_breakpoints].enabled = 1;
	dbg->num_breakpoints++;
	return 1;
}

/* Check if breakpoint at address */
static inline int debugger_check_breakpoint(debugger_t *dbg, unsigned short addr) {
	for (int i = 0; i < dbg->num_breakpoints; i++) {
		if (dbg->breakpoints[i].enabled && dbg->breakpoints[i].address == addr) {
			return 1;
		}
	}
	return 0;
}

/* Trace instruction execution */
static inline void debugger_trace_instruction(debugger_t *dbg, const cpu_t *cpu, 
	const char *mnemonic, unsigned char mode, unsigned long prev_cycles) {
	
	FILE *out = dbg->trace_file ? dbg->trace_file : stdout;
	
	fprintf(out, "%04X: %-3s ", cpu->pc, mnemonic);
	
	switch (mode) {
	case MODE_IMPLIED:
		fprintf(out, "      ");
		break;
	case MODE_IMMEDIATE:
		fprintf(out, "#$??  ");
		break;
	case MODE_ABSOLUTE:
	case MODE_ABSOLUTE_X:
	case MODE_ABSOLUTE_Y:
		fprintf(out, "$???? ");
		break;
	case MODE_ZP:
	case MODE_ZP_X:
	case MODE_ZP_Y:
		fprintf(out, "$??   ");
		break;
	case MODE_INDIRECT:
	case MODE_INDIRECT_X:
	case MODE_INDIRECT_Y:
	case MODE_ZP_INDIRECT:
	case MODE_ABS_INDIRECT_Y:
		fprintf(out, "($??) ");
		break;
	case MODE_RELATIVE:
		fprintf(out, "$??   ");
		break;
	default:
		fprintf(out, "      ");
	}
	
	unsigned long new_cycles = cpu->cycles - prev_cycles;
	fprintf(out, "| A=%02X X=%02X Y=%02X S=%02X P=%02X | CYC: %lu (+%lu)\n",
		cpu->a, cpu->x, cpu->y, cpu->s, cpu->p, cpu->cycles, new_cycles);
}

/* Open trace file */
static inline int debugger_open_trace_file(debugger_t *dbg, const char *filename) {
	dbg->trace_file = fopen(filename, "w");
	if (!dbg->trace_file) {
		return 0;
	}
	fprintf(dbg->trace_file, "=== Execution Trace ===\n");
	fprintf(dbg->trace_file, "PC   OP  ARG       | A    X    Y    S    P    | Cycles\n");
	fprintf(dbg->trace_file, "====================================================\n");
	return 1;
}

/* Close trace file */
static inline void debugger_close_trace_file(debugger_t *dbg) {
	if (dbg->trace_file) {
		fclose(dbg->trace_file);
		dbg->trace_file = NULL;
	}
}

#endif
