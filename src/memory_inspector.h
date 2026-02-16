#ifndef MEMORY_INSPECTOR_H
#define MEMORY_INSPECTOR_H

#include <stdio.h>
#include <stdint.h>
#include "memory.h"

typedef struct {
	uint16_t start_addr;
	uint16_t end_addr;
	int show_ascii;
	int show_hex;
} memory_view_t;

/* Initialize memory view */
static inline void memory_view_init(memory_view_t *view, uint16_t start, uint16_t end) {
	view->start_addr = start;
	view->end_addr = end;
	view->show_hex = 1;
	view->show_ascii = 1;
}

/* Display memory dump in hex and ASCII */
static inline void memory_dump(memory_t *mem, uint16_t start, uint16_t end) {
	printf("\n=== Memory Dump: 0x%04X - 0x%04X ===\n", start, end);
	printf("Addr  | Hex                              | ASCII\n");
	printf("------+----------------------------------+-----------------\n");
	
	for (uint16_t addr = start & 0xFFF0; addr <= end; addr += 16) {
		if (addr >= start && addr <= end) {
			printf("%04X  | ", addr);
			
			/* Hex display */
			for (int i = 0; i < 16; i++) {
				if (addr + i >= start && addr + i <= end) {
					printf("%02X ", mem->mem[addr + i]);
				} else {
					printf("   ");
				}
				if (i == 7) printf("| ");
			}
			
			/* ASCII display */
			printf("| ");
			for (int i = 0; i < 16; i++) {
				if (addr + i >= start && addr + i <= end) {
					unsigned char c = mem->mem[addr + i];
					if (c >= 32 && c < 127) {
						printf("%c", c);
					} else {
						printf(".");
					}
				} else {
					printf(" ");
				}
			}
			printf("\n");
		}
	}
	printf("\n");
}

/* Display single byte */
static inline void memory_read_byte(memory_t *mem, uint16_t addr) {
	unsigned char val = mem->mem[addr];
	printf("Memory[0x%04X] = 0x%02X (%3d, '%c')\n", addr, val, val,
		(val >= 32 && val < 127) ? val : '.');
}

/* Display word (2 bytes, little-endian) */
static inline void memory_read_word(memory_t *mem, uint16_t addr) {
	unsigned char lo = mem->mem[addr];
	unsigned char hi = mem->mem[addr + 1];
	uint16_t word = lo | (hi << 8);
	printf("Memory[0x%04X:0x%04X] = 0x%04X (%d)\n", addr, addr + 1, word, word);
}

/* Search memory for a value */
static inline int memory_search(memory_t *mem, uint16_t start, uint16_t end, 
	unsigned char value, uint16_t *results, int max_results) {
	
	int count = 0;
	for (uint16_t addr = start; addr <= end && count < max_results; addr++) {
		if (mem->mem[addr] == value) {
			results[count++] = addr;
		}
	}
	return count;
}

/* Display search results */
static inline void memory_search_display(uint16_t *results, int count, unsigned char value) {
	if (count == 0) {
		printf("No matches found for 0x%02X\n", value);
		return;
	}
	
	printf("Found %d matches for 0x%02X at:\n", count, value);
	for (int i = 0; i < count; i++) {
		printf("  0x%04X\n", results[i]);
	}
}

/* Watch variable (monitor address) */
typedef struct {
	uint16_t address;
	unsigned char last_value;
	int watching;
} watch_t;

static inline void watch_init(watch_t *watch, uint16_t addr) {
	watch->address = addr;
	watch->watching = 1;
	watch->last_value = 0;
}

static inline void watch_update(watch_t *watch, memory_t *mem) {
	if (!watch->watching) return;
	
	unsigned char current = mem->mem[watch->address];
	if (current != watch->last_value) {
		printf("[WATCH] 0x%04X changed: 0x%02X -> 0x%02X\n", 
			watch->address, watch->last_value, current);
		watch->last_value = current;
	}
}

#endif
