#include "opcodes.h"

static void lax_abs(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, arg);
	cpu->a = val;
	cpu->x = val;
	update_nz(cpu, val);
	cpu->pc += 3;
}

static void lax_abs_y(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, arg + cpu->y);
	cpu->a = val;
	cpu->x = val;
	update_nz(cpu, val);
	cpu->pc += 3;
}

static void lax_zp(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, arg & 0xFF);
	cpu->a = val;
	cpu->x = val;
	update_nz(cpu, val);
	cpu->pc += 2;
}

static void lax_zp_y(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, (arg + cpu->y) & 0xFF);
	cpu->a = val;
	cpu->x = val;
	update_nz(cpu, val);
	cpu->pc += 2;
}

static void lax_ind_y(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned short addr = mem_read(mem, arg) | (mem_read(mem, (arg + 1) & 0xFF) << 8);
	unsigned char val = mem_read(mem, addr + cpu->y);
	cpu->a = val;
	cpu->x = val;
	update_nz(cpu, val);
	cpu->pc += 2;
}

static void sax_abs(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	mem_write(mem, arg, cpu->a & cpu->x);
	cpu->pc += 3;
}

static void sax_zp(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	mem_write(mem, arg & 0xFF, cpu->a & cpu->x);
	cpu->pc += 2;
}

static void sax_zp_y(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	mem_write(mem, (arg + cpu->y) & 0xFF, cpu->a & cpu->x);
	cpu->pc += 2;
}

static void sax_ind_x(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned short addr = mem_read(mem, (arg + cpu->x) & 0xFF) |
		(mem_read(mem, (arg + cpu->x + 1) & 0xFF) << 8);
	mem_write(mem, addr, cpu->a & cpu->x);
	cpu->pc += 2;
}

static void dcm_abs(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = (mem_read(mem, arg) - 1) & 0xFF;
	mem_write(mem, arg, val);
	int result = cpu->a - val;
	set_flag(cpu, FLAG_C, cpu->a >= val);
	update_nz(cpu, result & 0xFF);
	cpu->pc += 3;
}

static void dcm_zp(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = (mem_read(mem, arg & 0xFF) - 1) & 0xFF;
	mem_write(mem, arg & 0xFF, val);
	int result = cpu->a - val;
	set_flag(cpu, FLAG_C, cpu->a >= val);
	update_nz(cpu, result & 0xFF);
	cpu->pc += 2;
}

static void ins_abs(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = (mem_read(mem, arg) + 1) & 0xFF;
	mem_write(mem, arg, val);
	int result = cpu->a - val;
	set_flag(cpu, FLAG_C, cpu->a >= val);
	update_nz(cpu, result & 0xFF);
	cpu->pc += 3;
}

static void ins_zp(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = (mem_read(mem, arg & 0xFF) + 1) & 0xFF;
	mem_write(mem, arg & 0xFF, val);
	int result = cpu->a - val;
	set_flag(cpu, FLAG_C, cpu->a >= val);
	update_nz(cpu, result & 0xFF);
	cpu->pc += 2;
}

static void asr_imm(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	cpu->a &= arg & 0xFF;
	set_flag(cpu, FLAG_C, cpu->a & 0x01);
	cpu->a >>= 1;
	update_nz(cpu, cpu->a);
	cpu->pc += 2;
}

static void arr_imm(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	cpu->a &= arg & 0xFF;
	int c = get_flag(cpu, FLAG_C);
	set_flag(cpu, FLAG_C, cpu->a & 0x01);
	cpu->a = ((cpu->a >> 1) | (c << 7)) & 0xFF;
	set_flag(cpu, FLAG_V, ((cpu->a >> 6) ^ (cpu->a >> 5)) & 1);
	update_nz(cpu, cpu->a);
	cpu->pc += 2;
}

static void alr_imm(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	cpu->a &= arg & 0xFF;
	set_flag(cpu, FLAG_C, cpu->a & 0x01);
	cpu->a >>= 1;
	update_nz(cpu, cpu->a);
	cpu->pc += 2;
}

static void anc_imm(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	cpu->a &= arg & 0xFF;
	set_flag(cpu, FLAG_C, cpu->a & 0x80);
	update_nz(cpu, cpu->a);
	cpu->pc += 2;
}

static void slo_abs(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, arg);
	set_flag(cpu, FLAG_C, val & 0x80);
	val = (val << 1) & 0xFF;
	mem_write(mem, arg, val);
	cpu->a |= val;
	update_nz(cpu, cpu->a);
	cpu->pc += 3;
}

static void slo_abs_x(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, arg + cpu->x);
	set_flag(cpu, FLAG_C, val & 0x80);
	val = (val << 1) & 0xFF;
	mem_write(mem, arg + cpu->x, val);
	cpu->a |= val;
	update_nz(cpu, cpu->a);
	cpu->pc += 3;
}

static void slo_abs_y(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, arg + cpu->y);
	set_flag(cpu, FLAG_C, val & 0x80);
	val = (val << 1) & 0xFF;
	mem_write(mem, arg + cpu->y, val);
	cpu->a |= val;
	update_nz(cpu, cpu->a);
	cpu->pc += 3;
}

static void slo_zp(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, arg & 0xFF);
	set_flag(cpu, FLAG_C, val & 0x80);
	val = (val << 1) & 0xFF;
	mem_write(mem, arg & 0xFF, val);
	cpu->a |= val;
	update_nz(cpu, cpu->a);
	cpu->pc += 2;
}

static void slo_zp_x(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, (arg + cpu->x) & 0xFF);
	set_flag(cpu, FLAG_C, val & 0x80);
	val = (val << 1) & 0xFF;
	mem_write(mem, (arg + cpu->x) & 0xFF, val);
	cpu->a |= val;
	update_nz(cpu, cpu->a);
	cpu->pc += 2;
}

static void slo_ind_x(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned short addr = mem_read(mem, (arg + cpu->x) & 0xFF) |
		(mem_read(mem, (arg + cpu->x + 1) & 0xFF) << 8);
	unsigned char val = mem_read(mem, addr);
	set_flag(cpu, FLAG_C, val & 0x80);
	val = (val << 1) & 0xFF;
	mem_write(mem, addr, val);
	cpu->a |= val;
	update_nz(cpu, cpu->a);
	cpu->pc += 2;
}

static void slo_ind_y(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned short addr = mem_read(mem, arg) | (mem_read(mem, (arg + 1) & 0xFF) << 8);
	unsigned char val = mem_read(mem, addr + cpu->y);
	set_flag(cpu, FLAG_C, val & 0x80);
	val = (val << 1) & 0xFF;
	mem_write(mem, addr + cpu->y, val);
	cpu->a |= val;
	update_nz(cpu, cpu->a);
	cpu->pc += 2;
}

static void sre_abs(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, arg);
	set_flag(cpu, FLAG_C, val & 0x01);
	val >>= 1;
	mem_write(mem, arg, val);
	cpu->a ^= val;
	update_nz(cpu, cpu->a);
	cpu->pc += 3;
}

static void sre_zp(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, arg & 0xFF);
	set_flag(cpu, FLAG_C, val & 0x01);
	val >>= 1;
	mem_write(mem, arg & 0xFF, val);
	cpu->a ^= val;
	update_nz(cpu, cpu->a);
	cpu->pc += 2;
}

static void rla_abs(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, arg);
	int c = get_flag(cpu, FLAG_C);
	set_flag(cpu, FLAG_C, val & 0x80);
	val = ((val << 1) | c) & 0xFF;
	mem_write(mem, arg, val);
	cpu->a &= val;
	update_nz(cpu, cpu->a);
	cpu->pc += 3;
}

static void rla_zp(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, arg & 0xFF);
	int c = get_flag(cpu, FLAG_C);
	set_flag(cpu, FLAG_C, val & 0x80);
	val = ((val << 1) | c) & 0xFF;
	mem_write(mem, arg & 0xFF, val);
	cpu->a &= val;
	update_nz(cpu, cpu->a);
	cpu->pc += 2;
}

static void rra_abs(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, arg);
	int c = get_flag(cpu, FLAG_C);
	set_flag(cpu, FLAG_C, val & 0x01);
	val = ((val >> 1) | (c << 7)) & 0xFF;
	mem_write(mem, arg, val);
	int result = cpu->a + val + get_flag(cpu, FLAG_C);
	set_flag(cpu, FLAG_C, result > 0xFF);
	set_flag(cpu, FLAG_V, ((cpu->a ^ result) & (val ^ result) & 0x80) != 0);
	cpu->a = result & 0xFF;
	update_nz(cpu, cpu->a);
	cpu->pc += 3;
}

static void rra_zp(cpu_t *cpu, memory_t *mem, unsigned short arg) {
	unsigned char val = mem_read(mem, arg & 0xFF);
	int c = get_flag(cpu, FLAG_C);
	set_flag(cpu, FLAG_C, val & 0x01);
	val = ((val >> 1) | (c << 7)) & 0xFF;
	mem_write(mem, arg & 0xFF, val);
	int result = cpu->a + val + get_flag(cpu, FLAG_C);
	set_flag(cpu, FLAG_C, result > 0xFF);
	set_flag(cpu, FLAG_V, ((cpu->a ^ result) & (val ^ result) & 0x80) != 0);
	cpu->a = result & 0xFF;
	update_nz(cpu, cpu->a);
	cpu->pc += 2;
}

extern void lda_imm(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void lda_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void lda_abs_x(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void lda_abs_y(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void lda_zp(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void lda_zp_x(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void lda_ind_x(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void lda_ind_y(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ldx_imm(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ldx_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ldx_abs_y(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ldx_zp(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ldx_zp_y(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ldy_imm(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ldy_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ldy_abs_x(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ldy_zp(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ldy_zp_x(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sta_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sta_abs_x(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sta_abs_y(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sta_zp(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sta_zp_x(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sta_ind_x(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sta_ind_y(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void stx_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void stx_zp(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void stx_zp_y(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sty_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sty_zp(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sty_zp_x(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void adc_imm(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void adc_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void adc_abs_x(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void adc_abs_y(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void adc_zp(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void adc_zp_x(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void adc_ind_x(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void adc_ind_y(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sbc_imm(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sbc_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void cmp_imm(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void cmp_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void cpx_imm(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void cpy_imm(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void inc_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ina(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void inx(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void iny(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void dec_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void dea(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void dex(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void dey(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void asl_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void asla(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void lsr_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void lsra(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void rol_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void rola(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ror_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void rora(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void and_imm(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void and_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void eor_imm(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void eor_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ora_imm(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void ora_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void bit_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void jmp_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void jsr_abs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void rts(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void bra(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void bcc(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void bcs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void beq(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void bne(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void bmi(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void bpl(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void bvs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void bvc(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void clc(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sec(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void cld(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sed(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void cli(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void sei(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void clv(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void tax(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void txa(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void tay(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void tya(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void tsx(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void txs(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void pha(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void pla(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void php(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void plp(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void brk(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void rti(cpu_t *cpu, memory_t *mem, unsigned short arg);
extern void nop(cpu_t *cpu, memory_t *mem, unsigned short arg);

opcode_handler_t opcodes_6502_undoc[] = {
	{"LDA", MODE_IMMEDIATE, lda_imm, 2},
	{"LDA", MODE_ABSOLUTE, lda_abs, 4},
	{"LDA", MODE_ABSOLUTE_X, lda_abs_x, 4},
	{"LDA", MODE_ABSOLUTE_Y, lda_abs_y, 4},
	{"LDA", MODE_ZP, lda_zp, 3},
	{"LDA", MODE_ZP_X, lda_zp_x, 4},
	{"LDA", MODE_INDIRECT_X, lda_ind_x, 6},
	{"LDA", MODE_INDIRECT_Y, lda_ind_y, 5},
	{"LDX", MODE_IMMEDIATE, ldx_imm, 2},
	{"LDX", MODE_ABSOLUTE, ldx_abs, 4},
	{"LDX", MODE_ABSOLUTE_Y, ldx_abs_y, 4},
	{"LDX", MODE_ZP, ldx_zp, 3},
	{"LDX", MODE_ZP_Y, ldx_zp_y, 4},
	{"LDY", MODE_IMMEDIATE, ldy_imm, 2},
	{"LDY", MODE_ABSOLUTE, ldy_abs, 4},
	{"LDY", MODE_ABSOLUTE_X, ldy_abs_x, 4},
	{"LDY", MODE_ZP, ldy_zp, 3},
	{"LDY", MODE_ZP_X, ldy_zp_x, 4},
	{"STA", MODE_ABSOLUTE, sta_abs, 4},
	{"STA", MODE_ABSOLUTE_X, sta_abs_x, 5},
	{"STA", MODE_ABSOLUTE_Y, sta_abs_y, 5},
	{"STA", MODE_ZP, sta_zp, 3},
	{"STA", MODE_ZP_X, sta_zp_x, 4},
	{"STA", MODE_INDIRECT_X, sta_ind_x, 6},
	{"STA", MODE_INDIRECT_Y, sta_ind_y, 6},
	{"STX", MODE_ABSOLUTE, stx_abs, 4},
	{"STX", MODE_ZP, stx_zp, 3},
	{"STX", MODE_ZP_Y, stx_zp_y, 4},
	{"STY", MODE_ABSOLUTE, sty_abs, 4},
	{"STY", MODE_ZP, sty_zp, 3},
	{"STY", MODE_ZP_X, sty_zp_x, 4},
	{"ADC", MODE_IMMEDIATE, adc_imm, 2},
	{"ADC", MODE_ABSOLUTE, adc_abs, 4},
	{"ADC", MODE_ABSOLUTE_X, adc_abs_x, 4},
	{"ADC", MODE_ABSOLUTE_Y, adc_abs_y, 4},
	{"ADC", MODE_ZP, adc_zp, 3},
	{"ADC", MODE_ZP_X, adc_zp_x, 4},
	{"ADC", MODE_INDIRECT_X, adc_ind_x, 6},
	{"ADC", MODE_INDIRECT_Y, adc_ind_y, 5},
	{"SBC", MODE_IMMEDIATE, sbc_imm, 2},
	{"SBC", MODE_ABSOLUTE, sbc_abs, 4},
	{"CMP", MODE_IMMEDIATE, cmp_imm, 2},
	{"CMP", MODE_ABSOLUTE, cmp_abs, 4},
	{"CPX", MODE_IMMEDIATE, cpx_imm, 2},
	{"CPY", MODE_IMMEDIATE, cpy_imm, 2},
	{"INC", MODE_ABSOLUTE, inc_abs, 6},
	{"INA", MODE_IMPLIED, ina, 2},
	{"INX", MODE_IMPLIED, inx, 2},
	{"INY", MODE_IMPLIED, iny, 2},
	{"DEC", MODE_ABSOLUTE, dec_abs, 6},
	{"DEA", MODE_IMPLIED, dea, 2},
	{"DEX", MODE_IMPLIED, dex, 2},
	{"DEY", MODE_IMPLIED, dey, 2},
	{"ASL", MODE_ABSOLUTE, asl_abs, 6},
	{"ASL", MODE_IMPLIED, asla, 2},
	{"LSR", MODE_ABSOLUTE, lsr_abs, 6},
	{"LSR", MODE_IMPLIED, lsra, 2},
	{"ROL", MODE_ABSOLUTE, rol_abs, 6},
	{"ROL", MODE_IMPLIED, rola, 2},
	{"ROR", MODE_ABSOLUTE, ror_abs, 6},
	{"ROR", MODE_IMPLIED, rora, 2},
	{"AND", MODE_IMMEDIATE, and_imm, 2},
	{"AND", MODE_ABSOLUTE, and_abs, 4},
	{"EOR", MODE_IMMEDIATE, eor_imm, 2},
	{"EOR", MODE_ABSOLUTE, eor_abs, 4},
	{"ORA", MODE_IMMEDIATE, ora_imm, 2},
	{"ORA", MODE_ABSOLUTE, ora_abs, 4},
	{"BIT", MODE_ABSOLUTE, bit_abs, 4},
	{"JMP", MODE_ABSOLUTE, jmp_abs, 3},
	{"JSR", MODE_ABSOLUTE, jsr_abs, 6},
	{"RTS", MODE_IMPLIED, rts, 6},
	{"BRA", MODE_RELATIVE, bra, 2},
	{"BCC", MODE_RELATIVE, bcc, 2},
	{"BCS", MODE_RELATIVE, bcs, 2},
	{"BEQ", MODE_RELATIVE, beq, 2},
	{"BNE", MODE_RELATIVE, bne, 2},
	{"BMI", MODE_RELATIVE, bmi, 2},
	{"BPL", MODE_RELATIVE, bpl, 2},
	{"BVS", MODE_RELATIVE, bvs, 2},
	{"BVC", MODE_RELATIVE, bvc, 2},
	{"CLC", MODE_IMPLIED, clc, 2},
	{"SEC", MODE_IMPLIED, sec, 2},
	{"CLD", MODE_IMPLIED, cld, 2},
	{"SED", MODE_IMPLIED, sed, 2},
	{"CLI", MODE_IMPLIED, cli, 2},
	{"SEI", MODE_IMPLIED, sei, 2},
	{"CLV", MODE_IMPLIED, clv, 2},
	{"TAX", MODE_IMPLIED, tax, 2},
	{"TXA", MODE_IMPLIED, txa, 2},
	{"TAY", MODE_IMPLIED, tay, 2},
	{"TYA", MODE_IMPLIED, tya, 2},
	{"TSX", MODE_IMPLIED, tsx, 2},
	{"TXS", MODE_IMPLIED, txs, 2},
	{"PHA", MODE_IMPLIED, pha, 3},
	{"PLA", MODE_IMPLIED, pla, 4},
	{"PHP", MODE_IMPLIED, php, 3},
	{"PLP", MODE_IMPLIED, plp, 4},
	{"BRK", MODE_IMPLIED, brk, 7},
	{"RTI", MODE_IMPLIED, rti, 6},
	{"NOP", MODE_IMPLIED, nop, 2},
	{"LAX", MODE_ABSOLUTE, lax_abs, 4},
	{"LAX", MODE_ABSOLUTE_Y, lax_abs_y, 4},
	{"LAX", MODE_ZP, lax_zp, 3},
	{"LAX", MODE_ZP_Y, lax_zp_y, 4},
	{"LAX", MODE_INDIRECT_Y, lax_ind_y, 5},
	{"SAX", MODE_ABSOLUTE, sax_abs, 4},
	{"SAX", MODE_ZP, sax_zp, 3},
	{"SAX", MODE_ZP_Y, sax_zp_y, 4},
	{"SAX", MODE_INDIRECT_X, sax_ind_x, 6},
	{"DCM", MODE_ABSOLUTE, dcm_abs, 6},
	{"DCM", MODE_ZP, dcm_zp, 5},
	{"INS", MODE_ABSOLUTE, ins_abs, 6},
	{"INS", MODE_ZP, ins_zp, 5},
	{"ASR", MODE_IMMEDIATE, asr_imm, 2},
	{"ARR", MODE_IMMEDIATE, arr_imm, 2},
	{"ALR", MODE_IMMEDIATE, alr_imm, 2},
	{"ANC", MODE_IMMEDIATE, anc_imm, 2},
	{"SLO", MODE_ABSOLUTE, slo_abs, 6},
	{"SLO", MODE_ABSOLUTE_X, slo_abs_x, 7},
	{"SLO", MODE_ABSOLUTE_Y, slo_abs_y, 7},
	{"SLO", MODE_ZP, slo_zp, 5},
	{"SLO", MODE_ZP_X, slo_zp_x, 6},
	{"SLO", MODE_INDIRECT_X, slo_ind_x, 8},
	{"SLO", MODE_INDIRECT_Y, slo_ind_y, 8},
	{"SRE", MODE_ABSOLUTE, sre_abs, 6},
	{"SRE", MODE_ZP, sre_zp, 5},
	{"RLA", MODE_ABSOLUTE, rla_abs, 6},
	{"RLA", MODE_ZP, rla_zp, 5},
	{"RRA", MODE_ABSOLUTE, rra_abs, 6},
	{"RRA", MODE_ZP, rra_zp, 5},
};

int OPCODES_6502_UNDOC_COUNT = sizeof(opcodes_6502_undoc) / sizeof(opcodes_6502_undoc[0]);
