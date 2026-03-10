#ifndef MEMORY_H
#define MEMORY_H

#include <stdlib.h>
#include <stdio.h>
#include "cpu.h"

/* Physical (raw) byte read — no MAP translation.
 * Used for far/flat 28-bit access and internally after MAP address translation. */
static inline unsigned char mem_read_phys(memory_t *mem, unsigned int phys) {
	if (phys < 0x10000)
		return mem->mem[phys];
	unsigned int page = phys >> FAR_PAGE_SHIFT;
	unsigned int off  = phys & (FAR_PAGE_SIZE - 1);
	if (!mem->far_pages[page])
		return 0;
	return mem->far_pages[page][off];
}

/* MEGA65 math coprocessor — recompute results whenever an input register is
 * written.  Registers are combinatorial on real hardware; we mirror that here.
 */
static inline void mem_math_update(memory_t *mem, unsigned short addr) {
	if (addr >= 0xD770 && addr <= 0xD773) {
		unsigned int a = mem->mem[0xD770] | ((unsigned int)mem->mem[0xD771] << 8);
		unsigned int b = mem->mem[0xD772] | ((unsigned int)mem->mem[0xD773] << 8);
		unsigned long long p = (unsigned long long)a * b;
		mem->mem[0xD778] = (unsigned char)(p);
		mem->mem[0xD779] = (unsigned char)(p >> 8);
		mem->mem[0xD77A] = (unsigned char)(p >> 16);
		mem->mem[0xD77B] = (unsigned char)(p >> 24);
	} else if (addr >= 0xD768 && addr <= 0xD76B) {
		unsigned int a = mem->mem[0xD768] | ((unsigned int)mem->mem[0xD769] << 8);
		unsigned int b = mem->mem[0xD76A] | ((unsigned int)mem->mem[0xD76B] << 8);
		unsigned int q = 0, r = 0;
		if (b) { q = a / b; r = a % b; }
		mem->mem[0xD778] = (unsigned char)(q);
		mem->mem[0xD779] = (unsigned char)(q >> 8);
		mem->mem[0xD77A] = (unsigned char)(r);
		mem->mem[0xD77B] = (unsigned char)(r >> 8);
	}
}

/* Forward declaration for DMA */
static inline void mem_write_phys(memory_t *mem, unsigned int phys, unsigned char val);

/* MEGA65 DMA execution */
#define DMA_MODE_LEGACY   0
#define DMA_MODE_ENHANCED 1

static inline void mem_dma_execute(memory_t *mem, unsigned char val, int mode) {
	unsigned int job_addr = val | (mem->mem[0xD701] << 8) | (mem->mem[0xD702] << 16) | ((mem->mem[0xD704] & 0x0F) << 24);
	unsigned int ptr = job_addr;
	int format = (mode == DMA_MODE_LEGACY) ? 11 : 12;
	unsigned int src_mb = 0, dst_mb = 0;

	unsigned char token = mem_read_phys(mem, ptr);
	if (mode == DMA_MODE_ENHANCED && (token == 0x0A || token == 0x0B)) {
		ptr++; format = (token == 0x0A) ? 11 : 12;
		token = mem_read_phys(mem, ptr++);
		while (token != 0x00 && ptr - job_addr < 256) {
			if (token == 0x80) src_mb = mem_read_phys(mem, ptr++);
			else if (token == 0x81) dst_mb = mem_read_phys(mem, ptr++);
			else if (token & 0x80) ptr++;
			token = mem_read_phys(mem, ptr++);
		}
	}

	unsigned char cmd = mem_read_phys(mem, ptr);
	unsigned int count = mem_read_phys(mem, ptr + 1) | (mem_read_phys(mem, ptr + 2) << 8);
	if (count == 0) count = 65536;

	unsigned int src = mem_read_phys(mem, ptr + 3) | (mem_read_phys(mem, ptr + 4) << 8) | 
	                  ((mem_read_phys(mem, ptr + 5) & 0x0F) << 16) | (src_mb << 20);
	unsigned int dst = mem_read_phys(mem, ptr + 6) | (mem_read_phys(mem, ptr + 7) << 8) | 
	                  ((mem_read_phys(mem, ptr + 8) & 0x0F) << 16) | (dst_mb << 20);

	if ((cmd & 0x03) == 0x00) {
		for (unsigned int i = 0; i < count; i++) mem_write_phys(mem, dst + i, mem_read_phys(mem, src + i));
	} else if ((cmd & 0x03) == 0x03 || (format == 11 && (cmd & 0x03) == 0x02)) {
		unsigned char fill_val = mem_read_phys(mem, ptr + 3);
		for (unsigned int i = 0; i < count; i++) mem_write_phys(mem, dst + i, fill_val);
	}
}

static inline void mem_write_phys(memory_t *mem, unsigned int phys, unsigned char val) {
	if (phys < 0x10000) {
		mem->mem[phys] = val;
		if (phys == 0xD702) mem->mem[0xD704] = 0;
		mem_math_update(mem, (unsigned short)phys);
		if (phys == 0xD705) mem_dma_execute(mem, val, DMA_MODE_ENHANCED);
		else if (phys == 0xD700) mem_dma_execute(mem, val, DMA_MODE_LEGACY);
		return;
	}
	unsigned int page = phys >> FAR_PAGE_SHIFT;
	unsigned int off  = phys & (FAR_PAGE_SIZE - 1);
	if (!mem->far_pages[page]) mem->far_pages[page] = (unsigned char *)calloc(FAR_PAGE_SIZE, 1);
	mem->far_pages[page][off] = val;
}

static inline unsigned char mem_read(memory_t *mem, unsigned short addr) {
	unsigned int block = (unsigned int)addr >> 13;
	if (mem->map_offset[block]) {
		unsigned int phys = ((unsigned int)addr + mem->map_offset[block]) & 0xFFFFF;
		return mem_read_phys(mem, phys);
	}
	return mem->mem[addr];
}

static inline void mem_write(memory_t *mem, unsigned short addr, unsigned char val) {
	unsigned int block = (unsigned int)addr >> 13;
	if (mem->mem_writes < 256) {
		mem->mem_addr[mem->mem_writes] = addr;
		mem->mem_old_val[mem->mem_writes] = mem_read(mem, addr);
		mem->mem_val[mem->mem_writes] = val;
	}
	mem->mem_writes++;
	if (mem->map_offset[block]) {
		unsigned int phys = ((unsigned int)addr + mem->map_offset[block]) & 0xFFFFF;
		mem_write_phys(mem, phys, val);
	} else {
		mem_write_phys(mem, addr, val);
	}
}

static inline unsigned char far_mem_read(memory_t *m, unsigned int addr) {
	return mem_read_phys(m, addr);
}

static inline void far_mem_write(memory_t *m, unsigned int addr, unsigned char val) {
	if (addr < 0x10000) {
		if (m->mem_writes < 256) {
			m->mem_addr[m->mem_writes] = (unsigned short)addr;
			m->mem_old_val[m->mem_writes] = far_mem_read(m, addr);
			m->mem_val[m->mem_writes] = val;
		}
		m->mem_writes++;
	}
	mem_write_phys(m, addr, val);
}

#endif
