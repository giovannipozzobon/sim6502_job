#include "catch.hpp"
#include "memory.h"
#include "io_registry.h"
#include <cstring>
#include <stdlib.h>

class MockIOHandler : public IOHandler {
public:
    uint8_t read_val = 0xAA;
    uint8_t last_written = 0;
    bool write_called = false;
    bool read_called = false;

    const char* get_handler_name() const override { return "MockIO"; }
    bool io_read(memory_t *mem, uint16_t addr, uint8_t *val) override {
        (void)mem; (void)addr;
        read_called = true;
        *val = read_val;
        return true;
    }
    bool io_write(memory_t *mem, uint16_t addr, uint8_t val) override {
        (void)mem; (void)addr;
        write_called = true;
        last_written = val;
        return true;
    }
};

TEST_CASE("Memory - Physical Access", "[memory]") {
    memory_t mem;
    memset(&mem, 0, sizeof(memory_t));

    SECTION("Low Memory Read/Write") {
        mem_write_phys(&mem, 0x1234, 0x55);
        CHECK(mem_read_phys(&mem, 0x1234) == 0x55);
        CHECK(mem.mem[0x1234] == 0x55);
    }

    SECTION("Sparse Far Memory") {
        // Test high address (beyond 64K)
        unsigned int addr = 0x1234567;
        mem_write_phys(&mem, addr, 0xBE);
        CHECK(mem_read_phys(&mem, addr) == 0xBE);
        
        unsigned int page = addr >> FAR_PAGE_SHIFT;
        CHECK(mem.far_pages[page] != nullptr);
        
        mem_free_far_pages(&mem);
        CHECK(mem.far_pages[page] == nullptr);
        // After free, it should return 0 (default for unallocated sparse pages)
        CHECK(mem_read_phys(&mem, addr) == 0);
    }
}

TEST_CASE("Memory - MAP Translation", "[memory]") {
    memory_t mem;
    memset(&mem, 0, sizeof(memory_t));

    SECTION("Basic Translation") {
        // Block 0: 0x0000 - 0x1FFF
        // Map block 0 to 0x40000 physical
        mem.map_offset[0] = 0x40000;
        
        mem_write(&mem, 0x1000, 0x42);
        // Should have written to 0x1000 + 0x40000 = 0x41000 physical
        CHECK(far_mem_read(&mem, 0x41000) == 0x42);
        CHECK(mem_read(&mem, 0x1000) == 0x42);
        
        // Logical 0x1000 should NOT be in mem.mem[0x1000] if MAP is active
        CHECK(mem.mem[0x1000] == 0);
    }

    SECTION("Wrapping") {
        // Map block 7 (0xE000-0xFFFF) so it wraps around 20-bit limit (0xFFFFF)
        // 0xF000 + 0xF1000 = 0x100000 -> 0x00000 (after & 0xFFFFF in memory.cpp)
        mem.map_offset[7] = 0xF1000;
        
        mem_write_phys(&mem, 0x0000, 0x99);
        CHECK(mem_read(&mem, 0xF000) == 0x99);
    }
}

TEST_CASE("Memory - I/O Registry", "[memory]") {
    memory_t mem;
    memset(&mem, 0, sizeof(memory_t));
    IORegistry registry(nullptr);
    mem.io_registry = &registry;

    MockIOHandler mock;
    registry.register_handler(0xD000, 0xD00F, &mock);
    registry.rebuild_map(&mem);

    SECTION("Handler Invocation") {
        mem_write(&mem, 0xD005, 0x21);
        CHECK(mock.write_called == true);
        CHECK(mock.last_written == 0x21);
        
        CHECK(mem_read(&mem, 0xD001) == 0xAA);
        CHECK(mock.read_called == true);
    }

    SECTION("Priority Overlap") {
        MockIOHandler high_pri;
        high_pri.read_val = 0xEE;
        
        registry.register_handler(0xD000, 0xD005, &high_pri, 10);
        registry.rebuild_map(&mem);
        
        // 0xD000 is now high_pri
        CHECK(mem_read(&mem, 0xD000) == 0xEE);
        // 0xD00F is still mock
        CHECK(mem_read(&mem, 0xD00F) == 0xAA);
    }
}
