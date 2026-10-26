#pragma once
#include "../../core/input_switcher.h"

class AdlTransport final : public lg::InputTransport {
public:
    AdlTransport(int adapter, int display) : adapter_(adapter), display_(display) {}
    lg::SwitchResult Send(const lg::InputCommand& command) override;
private:
    int adapter_;
    int display_;
};
