#ifndef COMMAND_REGISTRY_H
#define COMMAND_REGISTRY_H

#include <map>
#include <string>
#include <memory>
#include "CLICommand.h"

class CommandRegistry {
private:
    std::map<std::string, std::unique_ptr<CLICommand>> commands;
public:
    CommandRegistry();
    void registerCommand(std::unique_ptr<CLICommand> cmd);
    CLICommand* getCommand(const std::string& name);
    const std::map<std::string, std::unique_ptr<CLICommand>>& getAllCommands() const;
};

#endif
