#ifndef BREAKPOINT_H
#define BREAKPOINT_H

#include "cpu.h"
#include <stdio.h>

#define MAX_BREAKPOINTS 16

typedef struct {
	breakpoint_t breakpoints[MAX_BREAKPOINTS];
	int count;
} breakpoint_list_t;

/* Initialize breakpoint system */
static inline void breakpoint_init(breakpoint_list_t *bp_list) {
	bp_list->count = 0;
	for (int i = 0; i < MAX_BREAKPOINTS; i++) {
		bp_list->breakpoints[i].address = 0;
		bp_list->breakpoints[i].enabled = 0;
	}
}

/* Add a breakpoint at address */
static inline int breakpoint_add(breakpoint_list_t *bp_list, unsigned short address) {
	if (bp_list->count >= MAX_BREAKPOINTS) {
		return 0;  /* Too many breakpoints */
	}
	bp_list->breakpoints[bp_list->count].address = address;
	bp_list->breakpoints[bp_list->count].enabled = 1;
	bp_list->count++;
	return 1;
}

/* Remove a breakpoint at address */
static inline int breakpoint_remove(breakpoint_list_t *bp_list, unsigned short address) {
	for (int i = 0; i < bp_list->count; i++) {
		if (bp_list->breakpoints[i].address == address) {
			/* Shift remaining breakpoints down */
			for (int j = i; j < bp_list->count - 1; j++) {
				bp_list->breakpoints[j] = bp_list->breakpoints[j+1];
			}
			bp_list->count--;
			return 1;
		}
	}
	return 0;
}

/* Check if PC has breakpoint */
static inline int breakpoint_hit(breakpoint_list_t *bp_list, unsigned short pc) {
	for (int i = 0; i < bp_list->count; i++) {
		if (bp_list->breakpoints[i].enabled && 
		    bp_list->breakpoints[i].address == pc) {
			return 1;
		}
	}
	return 0;
}

/* List all breakpoints */
static inline void breakpoint_list(breakpoint_list_t *bp_list) {
	if (bp_list->count == 0) {
		printf("No breakpoints set.\n");
		return;
	}
	printf("Breakpoints:\n");
	for (int i = 0; i < bp_list->count; i++) {
		printf("  %d: 0x%04X %s\n", i, bp_list->breakpoints[i].address,
			bp_list->breakpoints[i].enabled ? "[enabled]" : "[disabled]");
	}
}

#endif
