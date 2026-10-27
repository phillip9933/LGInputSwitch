#include "adl_transport.h"
#include "../../amdddc/amdddc_core.h"
#include <sstream>

lg::SwitchResult AdlTransport::Send(const lg::InputCommand& command) {
    if (adapter_ < 0 || display_ < 0 || !lg::ValidCommand(command) || command.featureCode != 0xf4)
        return {lg::SwitchStatus::InvalidArgument, 2, "Invalid AMD target or LG input command."};
    // ADL and its packet implementation remain unchanged behind this boundary.
    const int rc = SetVcpFeatureWithI2cAddr(adapter_, display_,
        static_cast<unsigned short>(command.featureCode), command.value, command.sourceAddress);
    if (rc == 0) return {lg::SwitchStatus::CommandSent, 0, "Input command sent; monitor switching was not verified."};
    std::ostringstream detail;
    detail << "Switch failed: ADL status " << rc << ", adapter " << adapter_ << ", display " << display_
        << ", source 0x" << std::hex << command.sourceAddress << ", input 0x" << command.value;
    return {lg::SwitchStatus::TransportFailure, rc, detail.str()};
}
