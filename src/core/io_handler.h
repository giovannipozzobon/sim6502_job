#ifndef IO_HANDLER_H
#define IO_HANDLER_H

#include <stdint.h>

/* Forward declaration */
struct memory_t;

/**
 * IOHandler: Interface for memory-mapped I/O peripherals.
 */
class IOHandler {
public:
    virtual ~IOHandler() {}

    /**
     * Called when the CPU reads from a registered address.
     * Return true if the read was handled (value in *val),
     * false to fall back to standard memory.
     */
    virtual bool io_read(memory_t *mem, uint16_t addr, uint8_t *val) { (void)mem; (void)addr; (void)val; return false; }

    /**
     * Called when the CPU writes to a registered address.
     * Return true if the write was handled (intercepted),
     * false to allow standard memory write to also occur.
     */
    virtual bool io_write(memory_t *mem, uint16_t addr, uint8_t val) { (void)mem; (void)addr; (void)val; return false; }
};

#endif
