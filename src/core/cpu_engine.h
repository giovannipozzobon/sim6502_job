#ifndef CORE_CPU_H
#define CORE_CPU_H

#include "cpu.h"
#include "memory.h"
#include "disassembler.h"

/* --- Core Engine API --- */

/* Read instruction operand based on addressing mode. */
unsigned short decode_operand(cpu_t *cpu, memory_t *mem, unsigned char mode);

/* Fetch, decode, and execute one instruction. */
void execute_from_mem(cpu_t *cpu, memory_t *mem, const dispatch_table_t *dt, cpu_type_t cpu_type);

#endif
