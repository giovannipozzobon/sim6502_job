#ifndef MEMORY_H
#define MEMORY_H

typedef struct {
	unsigned char mem[0x10000];
	int mem_writes;
	unsigned short mem_addr[256];
	unsigned char mem_val[256];
} memory_t;

static inline void mem_write(memory_t *mem, unsigned short addr, unsigned char val) {
	mem->mem[addr] = val;
	if (mem->mem_writes < 256) {
		mem->mem_addr[mem->mem_writes] = addr;
		mem->mem_val[mem->mem_writes] = val;
		mem->mem_writes++;
	}
}

static inline unsigned char mem_read(memory_t *mem, unsigned short addr) {
	return mem->mem[addr];
}

#endif
