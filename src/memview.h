#ifndef MEMVIEW_H
#define MEMVIEW_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"

typedef struct {
	unsigned short start;
	unsigned short end;
	int display_ascii;
} memview_params_t;

/* Display memory range as hex dump with ASCII */
static inline void memview_dump(const memory_t *mem, unsigned short start, 
	unsigned short end, int show_ascii) {
	
	printf("\nMemory Dump: 0x%04X to 0x%04X\n", start, end);
	printf("============================================\n");
	printf("Address | 00 01 02 03 04 05 06 07 | ASCII\n");
	printf("--------|------------------------|---------\n");
	
	for (unsigned short addr = start; addr <= end; addr += 8) {
		printf("%04X    | ", addr);
		
		/* Print hex bytes */
		for (int i = 0; i < 8; i++) {
			if (addr + i <= end) {
				printf("%02X ", mem->mem[addr + i]);
			} else {
				printf("   ");
			}
		}
		
		printf("| ");
		
		/* Print ASCII */
		if (show_ascii) {
			for (int i = 0; i < 8; i++) {
				if (addr + i <= end) {
					unsigned char c = mem->mem[addr + i];
					if (c >= 32 && c < 127) {
						printf("%c", c);
					} else {
						printf(".");
					}
				}
			}
		}
		printf("\n");
	}
	printf("============================================\n");
}

/* Display single byte */
static inline void memview_byte(const memory_t *mem, unsigned short addr) {
	printf("\nMemory at 0x%04X: 0x%02X (%u)\n", addr, mem->mem[addr], mem->mem[addr]);
}

/* Display 16-bit word (little-endian) */
static inline void memview_word(const memory_t *mem, unsigned short addr) {
	unsigned char lo = mem->mem[addr];
	unsigned char hi = mem->mem[addr + 1];
	unsigned short value = lo | (hi << 8);
	printf("\nMemory at 0x%04X (16-bit): 0x%04X (%u)\n", addr, value, value);
}

/* Parse memory range from command line (e.g., "0x1000:0x1010" or "1000-1010") */
static inline int memview_parse_range(const char *range_str, unsigned short *start, 
	unsigned short *end) {
	
	char temp[256];
	strncpy(temp, range_str, sizeof(temp) - 1);
	temp[sizeof(temp) - 1] = 0;
	
	char *delim = strchr(temp, ':');
	if (!delim) {
		delim = strchr(temp, '-');
	}
	
	if (!delim) {
		fprintf(stderr, "Error: Use format 0x1000:0x1010 or 1000-1010\n");
		return 0;
	}
	
	*delim = 0;
	
	*start = (unsigned short)strtol(temp, NULL, 16);
	*end = (unsigned short)strtol(delim + 1, NULL, 16);
	
	if (*start > *end) {
		fprintf(stderr, "Error: Start address must be <= end address\n");
		return 0;
	}
	
	return 1;
}

/* Watch variable - monitor memory location */
typedef struct {
	unsigned short addr;
	unsigned char prev_value;
	unsigned char curr_value;
	int changed;
	char name[64];
} watch_t;

typedef struct {
	watch_t watches[16];
	int count;
} watch_list_t;

static inline void watch_init(watch_list_t *wl) {
	wl->count = 0;
	memset(wl->watches, 0, sizeof(wl->watches));
}

static inline int watch_add(watch_list_t *wl, unsigned short addr, const char *name) {
	if (wl->count >= 16) return 0;
	
	wl->watches[wl->count].addr = addr;
	wl->watches[wl->count].prev_value = 0;
	wl->watches[wl->count].curr_value = 0;
	wl->watches[wl->count].changed = 0;
	strncpy(wl->watches[wl->count].name, name, sizeof(wl->watches[0].name) - 1);
	
	wl->count++;
	return 1;
}

static inline void watch_update(watch_list_t *wl, const memory_t *mem) {
	for (int i = 0; i < wl->count; i++) {
		wl->watches[i].prev_value = wl->watches[i].curr_value;
		wl->watches[i].curr_value = mem->mem[wl->watches[i].addr];
		wl->watches[i].changed = (wl->watches[i].prev_value != wl->watches[i].curr_value);
	}
}

static inline void watch_display(const watch_list_t *wl) {
	if (wl->count == 0) {
		printf("No watches set.\n");
		return;
	}
	
	printf("\n=== Watched Variables ===\n");
	printf("Address | Name             | Value | Changed\n");
	printf("--------|------------------|-------|--------\n");
	
	for (int i = 0; i < wl->count; i++) {
		printf("%04X    | %-16s | 0x%02X  | %s\n",
			wl->watches[i].addr,
			wl->watches[i].name,
			wl->watches[i].curr_value,
			wl->watches[i].changed ? "YES" : "no");
	}
	printf("========================\n");
}

#endif
