#ifndef INTERRUPT_MANAGER_H
#define INTERRUPT_MANAGER_H

#include "cpu.h"
#include "memory.h"

/* Interrupt types */
typedef enum {
	INT_NONE = 0,
	INT_IRQ = 1,    /* Maskable interrupt - hardware */
	INT_NMI = 2,    /* Non-maskable interrupt */
	INT_BRK = 3     /* Software break */
} interrupt_type_t;

/* Interrupt vector addresses */
#define VECTOR_NMI      0xFFFA   /* Non-maskable interrupt */
#define VECTOR_RESET    0xFFFC   /* Reset */
#define VECTOR_IRQ      0xFFFE   /* IRQ/BRK */

typedef struct {
	interrupt_type_t type;
	unsigned short vector;
	int pending;
	unsigned long cycle_triggered;
	int irq_line;        /* 1 = IRQ pin asserted */
	int nmi_edge;        /* 1 = NMI edge detected */
} interrupt_state_t;

typedef struct {
	interrupt_state_t current;
	unsigned int interrupt_count;
	int in_handler;
} interrupt_manager_t;

/* Initialize interrupt manager */
static inline void interrupt_init(interrupt_manager_t *mgr) {
	mgr->current.type = INT_NONE;
	mgr->current.vector = 0;
	mgr->current.pending = 0;
	mgr->current.cycle_triggered = 0;
	mgr->current.irq_line = 0;
	mgr->current.nmi_edge = 0;
	mgr->interrupt_count = 0;
	mgr->in_handler = 0;
}

/* Request IRQ (maskable interrupt) */
static inline void interrupt_request_irq(interrupt_manager_t *mgr) {
	mgr->current.irq_line = 1;
}

/* Request NMI (non-maskable) */
static inline void interrupt_request_nmi(interrupt_manager_t *mgr) {
	mgr->current.nmi_edge = 1;
}

/* Check if interrupt should be taken */
static inline int interrupt_check(interrupt_manager_t *mgr, cpu_t *cpu) {
	/* NMI has priority over IRQ */
	if (mgr->current.nmi_edge) {
		mgr->current.type = INT_NMI;
		mgr->current.vector = VECTOR_NMI;
		mgr->current.pending = 1;
		mgr->current.nmi_edge = 0;
		return 1;
	}
	
	/* IRQ if line asserted and I flag clear */
	if (mgr->current.irq_line && !(cpu->p & 0x04)) {  /* I flag = bit 2 */
		mgr->current.type = INT_IRQ;
		mgr->current.vector = VECTOR_IRQ;
		mgr->current.pending = 1;
		return 1;
	}
	
	return 0;
}

/* Execute interrupt handler */
static inline void interrupt_handle(interrupt_manager_t *mgr, cpu_t *cpu, memory_t *mem) {
	if (!mgr->current.pending) return;
	
	/* Push return address (PC) to stack */
	mem_write(mem, 0x0100 + cpu->s, (cpu->pc >> 8) & 0xFF);
	cpu->s--;
	
	mem_write(mem, 0x0100 + cpu->s, cpu->pc & 0xFF);
	cpu->s--;
	
	/* Push processor status */
	unsigned char flags = cpu->p;
	
	/* Set B flag for BRK, clear for NMI */
	if (mgr->current.type == INT_BRK) {
		flags |= 0x10;   /* Set B flag */
	} else if (mgr->current.type == INT_NMI) {
		flags &= ~0x10;  /* Clear B flag for NMI */
	}
	
	mem_write(mem, 0x0100 + cpu->s, flags);
	cpu->s--;
	
	/* Set I flag (disable further IRQs) */
	cpu->p |= 0x04;
	
	/* Clear D flag (disable decimal mode) */
	cpu->p &= ~0x08;
	
	/* Read interrupt vector and jump */
	unsigned char vec_lo = mem_read(mem, mgr->current.vector);
	unsigned char vec_hi = mem_read(mem, mgr->current.vector + 1);
	cpu->pc = vec_lo | (vec_hi << 8);
	
	/* Add cycle overhead for interrupt (7 cycles) */
	cpu->cycles += 7;
	
	mgr->in_handler = 1;
	mgr->interrupt_count++;
	mgr->current.pending = 0;
}

/* Return from interrupt (RTI) */
static inline void interrupt_return(interrupt_manager_t *mgr, cpu_t *cpu, memory_t *mem) {
	/* Pop processor status */
	cpu->s++;
	cpu->p = mem_read(mem, 0x0100 + cpu->s);
	
	/* Pop return address */
	cpu->s++;
	unsigned char pc_lo = mem_read(mem, 0x0100 + cpu->s);
	
	cpu->s++;
	unsigned char pc_hi = mem_read(mem, 0x0100 + cpu->s);
	
	cpu->pc = pc_lo | (pc_hi << 8);
	
	/* Add cycle overhead for RTI (6 cycles) */
	cpu->cycles += 6;
	
	mgr->in_handler = 0;
}

/* Get interrupt name */
static inline const char *interrupt_type_name(interrupt_type_t type) {
	switch (type) {
	case INT_NONE: return "None";
	case INT_IRQ: return "IRQ (Maskable)";
	case INT_NMI: return "NMI (Non-Maskable)";
	case INT_BRK: return "BRK (Software)";
	default: return "Unknown";
	}
}

/* Display interrupt status */
static inline void interrupt_show_status(interrupt_manager_t *mgr) {
	printf("\n╔══════════════════════════════════════╗\n");
	printf("║     Interrupt Status                 ║\n");
	printf("╠══════════════════════════════════════╣\n");
	printf("║ Current Type:    %-21s║\n", interrupt_type_name(mgr->current.type));
	if (mgr->current.type != INT_NONE) {
		printf("║ Vector:          0x%04X              ║\n", mgr->current.vector);
		printf("║ Pending:         %-21s║\n", mgr->current.pending ? "Yes" : "No");
	}
	printf("║ IRQ Line:        %-21s║\n", mgr->current.irq_line ? "Asserted" : "Released");
	printf("║ NMI Edge:        %-21s║\n", mgr->current.nmi_edge ? "Pending" : "None");
	printf("║ Total Handled:   %-21u║\n", mgr->interrupt_count);
	printf("╚══════════════════════════════════════╝\n");
}

#endif
