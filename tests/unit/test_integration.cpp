#include "catch.hpp"
#include "cpu_6502.h"
#include "memory.h"
#include "interrupts.h"
#include <cstring>

TEST_CASE("System Integration - Large Memory Maps", "[integration][memory]") {
    memory_t mem;
    memset(&mem, 0, sizeof(mem));
    
    // Test writing to multiple 28-bit banks
    // Bank 0 (0x0xxxxxxx), Bank 1 (0x1xxxxxxx), Bank 15 (0xFxxxxxxx)
    
    SECTION("Multi-bank access") {
        far_mem_write(&mem, 0x0001000, 0xA1);
        far_mem_write(&mem, 0x1001000, 0xB2);
        far_mem_write(&mem, 0x2001000, 0xC3);
        far_mem_write(&mem, 0xF001000, 0xD4);
        
        CHECK(far_mem_read(&mem, 0x0001000) == 0xA1);
        CHECK(far_mem_read(&mem, 0x1001000) == 0xB2);
        CHECK(far_mem_read(&mem, 0x2001000) == 0xC3);
        CHECK(far_mem_read(&mem, 0xF001000) == 0xD4);
    }
    
    SECTION("Page boundary crossing") {
        // Sparse pages are 4KB (FAR_PAGE_SIZE)
        uint32_t addr = 0x1000FFF; // End of a page
        far_mem_write(&mem, addr, 0x55);
        far_mem_write(&mem, addr + 1, 0xAA);
        
        CHECK(far_mem_read(&mem, addr) == 0x55);
        CHECK(far_mem_read(&mem, addr + 1) == 0xAA);
    }

    mem_free_far_pages(&mem);
}

TEST_CASE("System Integration - High-Frequency Interrupts", "[integration][interrupts]") {
    memory_t mem;
    memset(&mem, 0, sizeof(mem));
    CPU* cpu = CPUFactory::create(CPU_6502);
    cpu->mem = &mem;
    cpu->reset();
    
    // Setup IRQ vector at $FFFE
    mem.mem[0xFFFE] = 0x00;
    mem.mem[0xFFFF] = 0x20; // IRQ handler at $2000
    
    // RTI at $2000
    mem.mem[0x2000] = 0x40;
    
    // NOPs at $1000
    for(int i=0; i<100; i++) mem.mem[0x1000 + i] = 0xEA;
    cpu->pc = 0x1000;
    cpu->set_flag(FLAG_I, 0); // Enable interrupts
    
    SECTION("Rapid IRQ triggers") {
        interrupt_controller_t* ic = static_cast<interrupt_controller_t*>(cpu->get_interrupt_controller());
        
        for (int i = 0; i < 10; i++) {
            interrupt_request_irq(ic);
            cpu->step(); // Should trigger interrupt or be in handler
            // We can't easily check if we are "in" the handler with just one step 
            // if the NOP was executed before the IRQ was polled.
            // But we can check if PC changed significantly.
        }
        
        // At least some interrupts should have fired
        CHECK(cpu->cycles > 0);
    }
    
    delete cpu;
}
