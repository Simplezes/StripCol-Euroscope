#pragma once

#include "EuroScopePlugIn.h"
#include "gateway/GatewayLink.h"
#include <string>

class AtcListSync
{
public:
    AtcListSync(EuroScopePlugIn::CPlugIn &plugin, GatewayLink &link);

    void Update(int tick);
    void Invalidate();

private:
    std::string Build();

    EuroScopePlugIn::CPlugIn &plugin_;
    GatewayLink &link_;
    std::string lastSent_;
    bool forced_ = true;
};
