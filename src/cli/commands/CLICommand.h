#ifndef CLI_COMMAND_H
#define CLI_COMMAND_H

#include <string>
#include <vector>
#include "cpu.h"
#include "memory.h"
#include "opcodes.h"
#include "breakpoints.h"
#include "symbols.h"
#include "disassembler.h"

class CLICommand {
public:
    virtual ~CLICommand() {}
    virtual std::string name() const = 0;
    virtual std::string help() const = 0;
    virtual bool execute(const std::vector<std::string>& args,
                         CPU *cpu, memory_t *mem, 
                         opcode_handler_t **p_handlers, int *p_num_handlers,
                         cpu_type_t *p_cpu_type, dispatch_table_t *dt,
                         breakpoint_list_t *breakpoints,
                         symbol_table_t *symbols) = 0;
};

#endif
