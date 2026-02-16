#ifndef MEMORY_VIEWER_H
#define MEMORY_VIEWER_H

#include <stdio.h>
#include "memory.h"

typedef struct {
	unsigned short start_addr;
	unsigned short end_addr;
	int enabled;
} watch_range_t;

#define MAX_WATCH_RANGES 8

typedef struct {
	watch_range_t ranges[MAX_WATCH_RANGES];
	int num_ranges;
	int show_all;
} memory_viewer_t;

/* Initialize memory viewer */
static inline void memory_viewer_init(memory_viewer_t *viewer) {
	viewer->num_ranges = 0;
	viewer->show_all = 0;
	for (int i = 0; i < MAX_WATCH_RANGES; i++) {
		viewer->ranges[i].enabled = 0;
	}
}

/* Add memory range to watch */
static inline int memory_viewer_add_range(memory_viewer_t *viewer, 
	unsigned short start, unsigned short end) {
	
	if (viewer->num_ranges >= MAX_WATCH_RANGES) {
		return 0;
	}
	if (start > end) {
		unsigned short tmp = start;
		start = end;
		end = tmp;
	}
	
	viewer->ranges[viewer->num_ranges].start_addr = start;
	viewer->ranges[viewer->num_ranges].end_addr = end;
	viewer->ranges[viewer->num_ranges].enabled = 1;
	viewer->num_ranges++;
	return 1;
}

/* Dump memory in hex format */
static inline void memory_dump(memory_t *mem, unsigned short start, unsigned short end) {
	printf("\nMemory Dump: 0x%04X to 0x%04X\n", start, end);
	printf("═══════════════════════════════════════════════════════════════\n");
	printf("Address  | Hex Values                          | ASCII\n");
	printf("─────────┼─────────────────────────────────────┼─────────────────\n");
	
	for (unsigned short addr = start; addr <= end; addr += 16) {
		printf("%04X     | ", addr);
		
		/* Hex values */
		for (int i = 0; i < 16 && addr + i <= end; i++) {
			printf("%02X ", mem->mem[addr + i]);
		}
		
		/* Spacing */
		int printed = (end - addr + 1 < 16) ? (end - addr + 1) : 16;
		for (int i = printed; i < 16; i++) {
			printf("   ");
		}
		
		printf("| ");
		
		/* ASCII representation */
		for (int i = 0; i < 16 && addr + i <= end; i++) {
			unsigned char c = mem->mem[addr + i];
			if (c >= 32 && c < 127) {
				printf("%c", c);
			} else {
				printf(".");
			}
		}
		printf("\n");
	}
	printf("═══════════════════════════════════════════════════════════════\n");
}

/* Quick memory peek - single byte */
static inline void memory_peek(memory_t *mem, unsigned short addr) {
	unsigned char val = mem->mem[addr];
	printf("Peek at 0x%04X: 0x%02X (%d, '%c')\n", 
		addr, val, val, (val >= 32 && val < 127) ? val : '.');
}

/* Memory poke - single byte write */
static inline void memory_poke(memory_t *mem, unsigned short addr, unsigned char val) {
	mem->mem[addr] = val;
	printf("Poke at 0x%04X: wrote 0x%02X\n", addr, val);
}

/* Search for byte pattern in memory */
static inline int memory_search(memory_t *mem, unsigned short start, unsigned short end,
	unsigned char pattern, unsigned short *result) {
	
	for (unsigned short addr = start; addr <= end; addr++) {
		if (mem->mem[addr] == pattern) {
			*result = addr;
			return 1;
		}
	}
	return 0;
}

/* Display memory statistics */
static inline void memory_stats(memory_t *mem) {
	printf("\nMemory Statistics:\n");
	printf("═══════════════════════════════════════════\n");
	printf("Total Memory:     65536 bytes\n");
	printf("Used (written):   %d bytes\n", mem->mem_writes);
	printf("Memory Changes:   %d locations modified\n", mem->mem_writes);
	
	/* Count non-zero bytes */
	int nonzero = 0;
	for (int i = 0; i < 0x10000; i++) {
		if (mem->mem[i] != 0) nonzero++;
	}
	printf("Non-zero Bytes:   %d (%.1f%%)\n", nonzero, 
		(nonzero * 100.0) / 65536.0);
	printf("═══════════════════════════════════════════\n");
}

/* Watch for writes to memory range */
static inline int memory_check_write(memory_viewer_t *viewer, unsigned short addr) {
	for (int i = 0; i < viewer->num_ranges; i++) {
		if (viewer->ranges[i].enabled &&
		    addr >= viewer->ranges[i].start_addr &&
		    addr <= viewer->ranges[i].end_addr) {
			return 1;  /* Write in watched range */
		}
	}
	return 0;
}

#endif
