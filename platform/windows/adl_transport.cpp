#include "adl_transport.h"
#include "adl_command.h"
#include <sstream>

lg::SwitchResult AdlTransport::Send(const lg::InputCommand &command)
{
    if (adapter_ < 0 || display_ < 0 || !lg::ValidCommand(command) ||
        (command.featureCode != 0xf4 && !(command.featureCode == 0x60 && command.sourceAddress == 0x51)))
        return {lg::SwitchStatus::InvalidArgument, 2, "Invalid AMD target or input switching command."};
    const int rc = SendAdlCommand(adapter_, display_, command);
    if (rc == 0)
        return {lg::SwitchStatus::CommandSent, 0, "Input command sent; monitor switching was not verified."};
    std::ostringstream detail;
    detail << "Switch failed: ADL status " << rc << ", adapter " << adapter_ << ", display " << display_
           << ", source 0x" << std::hex << command.sourceAddress << ", input 0x" << command.value;
    return {lg::SwitchStatus::TransportFailure, rc, detail.str()};
}
