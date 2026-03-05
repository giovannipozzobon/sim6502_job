#ifndef DISASSEMBLER_H
#define DISASSEMBLER_H

#include <stdint.h>
#include "cpu.h"
#include "memory.h"
#include "opcodes.h"

/* 
 * dispatch_entry_t: O(1) byte-indexed execution/disassembly entry.
 */
typedef struct {
	opcode_fn     fn;
	unsigned char mode;
	const char   *mnemonic;
	unsigned char cycles;
} dispatch_entry_t;

typedef struct {
	dispatch_entry_t base[256];     /* indexed by single opcode byte */
	dispatch_entry_t quad[256];     /* 45GS02: $42 $42 prefix, indexed by 3rd byte */
	dispatch_entry_t quad_eom[256]; /* 45GS02: $42 $42 $EA prefix, indexed by 4th byte */
} dispatch_table_t;

/* --- Dispatch Table Management --- */

void dispatch_build(dispatch_table_t *dt, const opcode_handler_t *handlers, int n, cpu_type_t cpu_type);
const dispatch_entry_t *peek_dispatch(const cpu_t *cpu, const memory_t *mem, const dispatch_table_t *dt, cpu_type_t cpu_type);

/* --- Disassembler API --- */

/* 
 * disasm_one: disassemble one instruction at 'addr'.
 * buf: output buffer
 * Returns the number of bytes consumed.
 */
int disasm_one(const memory_t *mem, const dispatch_table_t *dt,
               cpu_type_t cpu_type, unsigned short addr,
               char *buf, int bufsz);

#endif
