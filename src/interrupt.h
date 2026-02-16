#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "cpu.h"
#include "memory.h"

/* Interrupt types */
typedef enum {
	IRQ_NONE = 0,
	IRQ_IRQ = 1,      /* Maskable interrupt */
	IRQ_NMI = 2,      /* Non-maskable interrupt */
	IRQ_RESET = 3     /* Reset */
} irq_type_t;

typedef struct {
	irq_type_t type;
	unsigned short vector;     /* Where to jump */
	int pending;
	int triggered_at_cycle;    /* Cycle when triggered */
} interrupt_t;

/* Interrupt controller */
typedef struct {
	interrupt_t current;
	int irq_line;              /* IRQ line state (0 = high, 1 = low) */
	int nmi_line;              /* NMI line state */
	int nmi_edge_triggered;    /* NMI edge detection */
	unsigned long cycles;      /* Current cycle count */
} interrupt_controller_t;

/* Initialize interrupt controller */
static inline void irq_init(interrupt_controller_t *irq) {
	irq->current.type = IRQ_NONE;
	irq->current.vector = 0;
	irq->current.pending = 0;
	irq->current.triggered_at_cycle = 0;
	irq->irq_line = 0;
	irq->nmi_line = 0;
	irq->nmi_edge_triggered = 0;
	irq->cycles = 0;
}

/* Request IRQ (maskable interrupt) */
static inline void irq_request_irq(interrupt_controller_t *irq) {
	irq->irq_line = 1;
}

/* Release IRQ line */
static inline void irq_release_irq(interrupt_controller_t *irq) {
	irq->irq_line = 0;
}

/* Request NMI (non-maskable interrupt) */
static inline void irq_request_nmi(interrupt_controller_t *irq) {
	irq->nmi_edge_triggered = 1;  /* NMI triggers on falling edge */
}

/* Check if interrupt should be taken */
static inline int irq_should_take_interrupt(interrupt_controller_t *irq, cpu_t *cpu) {
	/* NMI always has priority over IRQ */
	if (irq->nmi_edge_triggered) {
		irq->current.type = IRQ_NMI;
		irq->current.vector = 0xFFFA;  /* NMI vector */
		irq->current.pending = 1;
		irq->nmi_edge_triggered = 0;
		return 1;
	}
	
	/* IRQ if line asserted and I flag clear */
	if (irq->irq_line && !(cpu->p & 0x04)) {  /* I flag = bit 2 */
		irq->current.type = IRQ_IRQ;
		irq->current.vector = 0xFFFE;  /* IRQ vector */
		irq->current.pending = 1;
		return 1;
	}
	
	return 0;
}

/* Handle interrupt - push PC and flags, jump to handler */
static inline void irq_handle(interrupt_controller_t *irq, cpu_t *cpu, memory_t *mem) {
	if (!irq->current.pending) return;
	
	/* Push PC high byte */
	mem_write(mem, 0x100 + cpu->s, (cpu->pc >> 8) & 0xFF);
	cpu->s--;
	
	/* Push PC low byte */
	mem_write(mem, 0x100 + cpu->s, cpu->pc & 0xFF);
	cpu->s--;
	
	/* Push processor status with B flag set (for BRK-like behavior) */
	unsigned char status = cpu->p | 0x10;  /* Set B flag */
	if (irq->current.type == IRQ_NMI) {
		status &= ~0x10;  /* NMI doesn't set B flag */
	}
	
	mem_write(mem, 0x100 + cpu->s, status);
	cpu->s--;
	
	/* Set I flag (disable further IRQs) */
	cpu->p |= 0x04;
	
	/* Jump to interrupt handler */
	unsigned short lo = mem_read(mem, irq->current.vector);
	unsigned short hi = mem_read(mem, irq->current.vector + 1);
	cpu->pc = lo | (hi << 8);
	
	/* Add cycles for interrupt handling */
	cpu->cycles += 7;
	
	irq->current.pending = 0;
}

/* Return from interrupt handler (RTI) */
static inline void irq_rti(interrupt_controller_t *irq, cpu_t *cpu, memory_t *mem) {
	/* Pop processor status */
	cpu->s++;
	cpu->p = mem_read(mem, 0x100 + cpu->s);
	
	/* Pop PC low byte */
	cpu->s++;
	unsigned char pc_lo = mem_read(mem, 0x100 + cpu->s);
	
	/* Pop PC high byte */
	cpu->s++;
	unsigned char pc_hi = mem_read(mem, 0x100 + cpu->s);
	
	cpu->pc = pc_lo | (pc_hi << 8);
	
	/* Add cycles for RTI */
	cpu->cycles += 6;
}

/* Get interrupt info for display */
static inline const char *irq_type_name(irq_type_t type) {
	switch (type) {
	case IRQ_NONE: return "None";
	case IRQ_IRQ: return "IRQ (maskable)";
	case IRQ_NMI: return "NMI (non-maskable)";
	case IRQ_RESET: return "RESET";
	default: return "Unknown";
	}
}

/* Display interrupt status */
static inline void irq_display_status(const interrupt_controller_t *irq) {
	printf("\n=== Interrupt Status ===\n");
	printf("Current: %s\n", irq_type_name(irq->current.type));
	if (irq->current.type != IRQ_NONE) {
		printf("Vector: 0x%04X\n", irq->current.vector);
		printf("Pending: %s\n", irq->current.pending ? "Yes" : "No");
	}
	printf("IRQ Line: %s\n", irq->irq_line ? "Asserted" : "Released");
	printf("NMI Edge: %s\n", irq->nmi_edge_triggered ? "Pending" : "None");
	printf("=======================\n");
}

#endif
