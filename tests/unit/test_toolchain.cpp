#include "catch.hpp"
#include "symbols.h"
#include "disassembler.h"
#include "list_parser.h"
#include "cpu_6502.h"
#include <cstring>
#include <cstdio>

TEST_CASE("Toolchain - Symbol Table", "[toolchain][symbols]") {
    symbol_table_t st;
    symbol_table_init(&st, "6502");

    SECTION("Add and Lookup") {
        symbol_add(&st, "RESET", 0xFFFC, SYM_LABEL, "Reset vector");
        symbol_add(&st, "entry", 0x1000, SYM_LABEL, "Entry point");

        unsigned short addr;
        CHECK(symbol_lookup_name(&st, "RESET", &addr) == 1);
        CHECK(addr == 0xFFFC);
        
        char name[MAX_SYMBOL_NAME];
        CHECK(symbol_lookup_addr(&st, 0x1000, name) == 1);
        CHECK(strcmp(name, "entry") == 0);
    }

    SECTION("Case Sensitivity") {
        symbol_add(&st, "MyLabel", 0x2000, SYM_LABEL, "");
        unsigned short addr;
        // Currently symbol_lookup_name is case-sensitive
        CHECK(symbol_lookup_name(&st, "mylabel", &addr) == 0);
        CHECK(symbol_lookup_name(&st, "MyLabel", &addr) == 1);
    }
}

TEST_CASE("Toolchain - Disassembler", "[toolchain][disasm]") {
    memory_t mem;
    memset(&mem, 0, sizeof(memory_t));
    CPU* cpu = CPUFactory::create(CPU_6502);
    dispatch_table_t* dt = cpu->dispatch_table();

    SECTION("6502 Opcodes") {
        mem.mem[0x1000] = 0xA9; // LDA #$42
        mem.mem[0x1001] = 0x42;
        
        disasm_entry_t entry;
        disasm_one_entry(&mem, dt, CPU_6502, 0x1000, &entry);
        
        CHECK(strcmp(entry.mnemonic, "LDA") == 0);
        CHECK(strcmp(entry.operand, "#$42") == 0);
        CHECK(entry.size == 2);
    }

    SECTION("65C02 Opcodes") {
        CPU* cpu_c02 = CPUFactory::create(CPU_65C02);
        dispatch_table_t* dt_c02 = cpu_c02->dispatch_table();

        mem.mem[0x1000] = 0x80; // BRA $1005
        mem.mem[0x1001] = 0x03;
        
        disasm_entry_t entry;
        disasm_one_entry(&mem, dt_c02, CPU_65C02, 0x1000, &entry);
        
        CHECK(strcmp(entry.mnemonic, "BRA") == 0);
        CHECK(entry.size == 2);
        delete cpu_c02;
    }

    delete cpu;
}

TEST_CASE("Toolchain - List Parser", "[toolchain][parser]") {
    source_map_t *sm = (source_map_t*)malloc(sizeof(source_map_t));
    symbol_table_t st;
    source_map_init(sm);
    symbol_table_init(&st, "6502");

    const char* list_file = "test_acme.lst";
    FILE* f = fopen(list_file, "w");
    fprintf(f, "   1  1000  a9 01       lda #$01\n");
    fprintf(f, "   2  1002  8d 00 d0    sta $d000 ; @inspect\n");
    fprintf(f, "   3  1005  60          rts       ; @trap\n");
    fclose(f);

    SECTION("ACME List Loading") {
        bool ok = source_map_load_acme_list(sm, &st, list_file);
        CHECK(ok == true);
        CHECK(sm->count >= 3);

        char path[MAX_SOURCE_PATH];
        int line;
        CHECK(source_map_lookup_addr(sm, 0x1000, path, &line) == true);
        CHECK(line == 1);

        uint32_t addr;
        CHECK(source_map_lookup_line(sm, nullptr, 2, &addr) == true);
        CHECK(addr == 0x1002);
    }

    SECTION("Automatic Symbols") {
        source_map_load_acme_list(sm, &st, list_file);
        
        unsigned short addr;
        // The parser adds symbols for @inspect and @trap
        CHECK(symbol_lookup_name(&st, "inspect_1002", &addr) == 1);
        CHECK(symbol_lookup_name(&st, "trap_1005", &addr) == 1);
    }

    remove(list_file);
    free(sm);
}
