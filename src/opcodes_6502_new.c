#include "opcodes.h"

/* Load A */
void lda_imm(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	cpu->a = arg & 0xFF;
	update_nz(cpu, cpu->a);
	cpu->cycles += 2;
	cpu->pc += 2;
	cpu->cycles += 2;
}

void lda_abs(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	cpu->a = mem_read(mem, arg);
	update_nz(cpu, cpu->a);
	cpu->cycles += 4;
	cpu->pc += 3;
	cpu->cycles += 4;
}

void lda_abs_x(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	cpu->a = mem_read(mem, arg + cpu->x);
	update_nz(cpu, cpu->a);
	cpu->cycles += 4;
	cpu->pc += 3;
	cpu->cycles += (page_crossed(arg, arg + cpu->x) ? 5 : 4);
}

void lda_abs_y(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	cpu->a = mem_read(mem, arg + cpu->y);
	update_nz(cpu, cpu->a);
	cpu->cycles += 4;
	cpu->pc += 3;
	cpu->cycles += (page_crossed(arg, arg + cpu->y) ? 5 : 4);
}

void lda_zp(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	cpu->a = mem_read(mem, arg & 0xFF);
	update_nz(cpu, cpu->a);
	cpu->cycles += 3;
	cpu->pc += 2;
	cpu->cycles += 3;
}

void lda_zp_x(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	cpu->a = mem_read(mem, (arg + cpu->x) & 0xFF);
	update_nz(cpu, cpu->a);
	cpu->cycles += 4;
	cpu->pc += 2;
	cpu->cycles += 4;
}

void lda_ind_x(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned short addr = mem_read(mem, (arg + cpu->x) & 0xFF) |
		(mem_read(mem, (arg + cpu->x + 1) & 0xFF) << 8);
	cpu->a = mem_read(mem, addr);
	update_nz(cpu, cpu->a);
	cpu->cycles += 6;
	cpu->pc += 2;
	cpu->cycles += 6;
}

void lda_ind_y(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned short addr = mem_read(mem, arg) | (mem_read(mem, (arg + 1) & 0xFF) << 8);
	cpu->a = mem_read(mem, addr + cpu->y);
	update_nz(cpu, cpu->a);
	cpu->cycles += 5;
	cpu->pc += 2;
	cpu->cycles += (page_crossed(addr, addr + cpu->y) ? 6 : 5);
}

/* ... (rest of implementation follows similar pattern) ... */

opcode_handler_t opcodes_6502[] = {
	{"LDA", MODE_IMMEDIATE, lda_imm, 2, 2, 2, 2},
	{"LDA", MODE_ABSOLUTE, lda_abs, 4, 4, 4, 4},
	{"LDA", MODE_ABSOLUTE_X, lda_abs_x, 4, 4, 4, 4},
	{"LDA", MODE_ABSOLUTE_Y, lda_abs_y, 4, 4, 4, 4},
	{"LDA", MODE_ZP, lda_zp, 3, 3, 3, 3},
	{"LDA", MODE_ZP_X, lda_zp_x, 4, 4, 4, 4},
	{"LDA", MODE_INDIRECT_X, lda_ind_x, 6, 6, 6, 6},
	{"LDA", MODE_INDIRECT_Y, lda_ind_y, 5, 5, 5, 5},
	/* ... more instructions ... */
};

int OPCODES_6502_COUNT = sizeof(opcodes_6502) / sizeof(opcodes_6502[0]);
