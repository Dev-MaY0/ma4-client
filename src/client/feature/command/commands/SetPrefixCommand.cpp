#include "pch.h"
#include "SetPrefixCommand.h"
#include "../CommandManager.h"
#include "client/Ma4.h"

SetPrefixCommand::SetPrefixCommand()
    : Command("setprefix", LocalizeString::get("client.commands.setPrefix.desc"), "$ [prefix]") {
}

bool SetPrefixCommand::execute(std::string const label, std::vector<std::string> args) {
    if (args.empty()) {
        message(util::FormatWString(util::WFormat(LocalizeString::get("client.commands.setPrefix.success.name")),
                                    { util::StrToWStr(Ma4::getCommandManager().prefix) }));
        return true;
    } else {
        Ma4::getCommandManager().prefix = args[0];
        message(util::FormatWString(util::WFormat(LocalizeString::get("client.commands.setPrefix.success.name")),
                                    { util::StrToWStr(args[0]) }));
        return true;
    }
    return false;
}
