#ifndef BREAK_CMD_H
#define BREAK_CMD_H

#include "CLICommand.h"

class BreakCmd : public CLICommand {
public:
    virtual std::string name() const override { return "break"; }
    virtual std::string help() const override { return "break <addr> [condition] - Set a breakpoint"; }
    virtual bool execute(const std::vector<std::string>& args,
                         CPU *cpu, memory_t *mem, 
                         opcode_handler_t **p_handlers, int *p_num_handlers,
                         cpu_type_t *p_cpu_type, dispatch_table_t *dt,
                         breakpoint_list_t *breakpoints,
                         symbol_table_t *symbols) override;
};

#endif
