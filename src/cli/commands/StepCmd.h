#ifndef STEP_CMD_H
#define STEP_CMD_H

#include "CLICommand.h"

class StepCmd : public CLICommand {
public:
    virtual std::string name() const override { return "step"; }
    virtual std::string help() const override { return "step [n] - Execute n instructions (default 1)"; }
    virtual bool execute(const std::vector<std::string>& args,
                         CPU *cpu, memory_t *mem, 
                         opcode_handler_t **p_handlers, int *p_num_handlers,
                         cpu_type_t *p_cpu_type, dispatch_table_t *dt,
                         breakpoint_list_t *breakpoints,
                         symbol_table_t *symbols) override;
};

#endif
