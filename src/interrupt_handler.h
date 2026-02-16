#ifndef INTERRUPT_HANDLER_H
#define INTERRUPT_HANDLER_H

#include <stdio.h>
#include <stdint.h>
#include "cpu.h"
#include "memory.h"

/* Interrupt types */
typedef enum {
	INT_NONE = 0,
	INT_IRQ = 1,    /* Maskable interrupt */
	INT_NMI = 2,    /* Non-maskable interrupt */
	INT_BRK = 3     /* Software break */
} interrupt_type_t;

typedef struct {
	interrupt_type_t type;
	uint16_t vector;
	int pending;
	unsigned long triggered_at_cycle;
} interrupt_state_t;

typedef struct {
	interrupt_state_t current;
	int irq_line;          /* 1 = IRQ asserted */
	int nmi_pending;       /* 1 = NMI edge detected */
	int in_handler;        /* 1 = currently handling interrupt */
} interrupt_handler_t;

/* Initialize interrupt handler */
static inline void interrupt_init(interrupt_handler_t *handler) {
	handler->current.type = INT_NONE;
	handler->current.vector = 0;
	handler->current.pending = 0;
	handler->current.triggered_at_cycle = 0;
	handler->irq_line = 0;
	handler->nmi_pending = 0;
	handler->in_handler = 0;
}

/* Request IRQ (maskable) */
static inline void interrupt_request_irq(interrupt_handler_t *handler) {
	handler->irq_line = 1;
}

/* Release IRQ line */
static inline void interrupt_release_irq(interrupt_handler_t *handler) {
	handler->irq_line = 0;
}

/* Request NMI (non-maskable) - triggers on edge */
static inline void interrupt_request_nmi(interrupt_handler_t *handler) {
	handler->nmi_pending = 1;
}

/* Check if interrupt should be taken */
static inline int interrupt_pending(interrupt_handler_t *handler, cpu_t *cpu) {
	/* NMI always has priority */
	if (handler->nmi_pending) {
		handler->current.type = INT_NMI;
		handler->current.vector = 0xFFFA;  /* NMI vector */
		handler->current.pending = 1;
		handler->nmi_pending = 0;
		return 1;
	}
	
	/* IRQ if line asserted and I flag clear */
	if (handler->irq_line && !(cpu->p & 0x04)) {  /* I flag = bit 2 */
		handler->current.type = INT_IRQ;
		handler->current.vector = 0xFFFE;  /* IRQ/BRK vector */
		handler->current.pending = 1;
		handler->irq_line = 0;
		return 1;
	}
	
	return 0;
}

/* Execute interrupt handler */
static inline void interrupt_execute(interrupt_handler_t *handler, cpu_t *cpu, memory_t *mem) {
	if (!handler->current.pending) return;
	
	/* Push return address (PC - 1 for most interrupts, exact for NMI/IRQ) */
	unsigned char pc_high = (cpu->pc >> 8) & 0xFF;
	unsigned char pc_low = cpu->pc & 0xFF;
	
	mem_write(mem, 0x0100 + cpu->s, pc_high);
	cpu->s--;
	
	mem_write(mem, 0x0100 + cpu->s, pc_low);
	cpu->s--;
	
	/* Push processor status */
	unsigned char status = cpu->p;
	
	/* For BRK, set B flag; for NMI don't set B */
	if (handler->current.type == INT_BRK) {
		status |= 0x10;  /* Set B flag */
	}
	
	mem_write(mem, 0x0100 + cpu->s, status);
	cpu->s--;
	
	/* Set I flag (disable further IRQs) */
	cpu->p |= 0x04;
	
	/* Clear D flag (decimal mode) */
	cpu->p &= ~0x08;
	
	/* Read interrupt vector and jump */
	unsigned char vec_lo = mem_read(mem, handler->current.vector);
	unsigned char vec_hi = mem_read(mem, handler->current.vector + 1);
	cpu->pc = vec_lo | (vec_hi << 8);
	
	/* Add cycles for interrupt handling */
	cpu->cycles += 7;
	
	handler->in_handler = 1;
	handler->current.pending = 0;
}

/* Return from interrupt (RTI) */
static inline void interrupt_return(interrupt_handler_t *handler, cpu_t *cpu, memory_t *mem) {
	/* Pop processor status */
	cpu->s++;
	cpu->p = mem_read(mem, 0x0100 + cpu->s);
	
	/* Pop return address */
	cpu->s++;
	unsigned char pc_low = mem_read(mem, 0x0100 + cpu->s);
	
	cpu->s++;
	unsigned char pc_high = mem_read(mem, 0x0100 + cpu->s);
	
	cpu->pc = pc_low | (pc_high << 8);
	
	/* Add cycles for RTI */
	cpu->cycles += 6;
	
	handler->in_handler = 0;
}

/* Get interrupt name */
static inline const char *interrupt_name(interrupt_type_t type) {
	switch (type) {
	case INT_NONE: return "None";
	case INT_IRQ: return "IRQ (Maskable)";
	case INT_NMI: return "NMI (Non-Maskable)";
	case INT_BRK: return "BRK (Software)";
	default: return "Unknown";
	}
}

/* Display interrupt status */
static inline void interrupt_display_status(interrupt_handler_t *handler) {
	printf("\n=== Interrupt Status ===\n");
	printf("Current Type: %s\n", interrupt_name(handler->current.type));
	if (handler->current.type != INT_NONE) {
		printf("Vector: 0x%04X\n", handler->current.vector);
		printf("Pending: %s\n", handler->current.pending ? "Yes" : "No");
	}
	printf("IRQ Line: %s\n", handler->irq_line ? "Asserted" : "Released");
	printf("NMI Pending: %s\n", handler->nmi_pending ? "Yes" : "No");
	printf("In Handler: %s\n", handler->in_handler ? "Yes" : "No");
	printf("=======================\n");
}

#endif
