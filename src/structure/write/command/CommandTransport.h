#pragma once
#include "structure/write/command/CommandWriteSession.h"
class ClientInstance;
class LocalPlayer;
namespace lholo::structure::write::command {
struct Receipt { std::string id; Reply reply; std::string error; };
bool installCommandTransport();
void uninstallCommandTransport();
void sendCommand(ClientInstance&, LocalPlayer&, Dispatch const&);
std::optional<Receipt> takeCommandReceipt();
void clearCommandReceipts();
void forgetCommand(std::string const& id);
}
