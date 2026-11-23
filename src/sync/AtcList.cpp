#include "pch.h"
#include "plugin/StripCol.h"
#include <nlohmann/json.hpp>

using namespace EuroScopePlugIn;
using json = nlohmann::json;

void StripCol::SendAtcList(bool force)
{
    json data = json::array();
    CController ctrl = ControllerSelectFirst();
    while (ctrl.IsValid())
    {
        double freq = ctrl.GetPrimaryFrequency();
        char freqBuf[16];
        snprintf(freqBuf, sizeof(freqBuf), "%.3f", freq);

        json j;
        j["callsign"] = ctrl.GetCallsign();
        j["positionId"] = ctrl.GetPositionId();
        j["fullName"] = ctrl.GetFullName();
        j["rating"] = ctrl.GetRating();
        j["frequency"] = freqBuf;
        data.push_back(j);

        ctrl = ControllerSelectNext(ctrl);
    }

    json root;
    root["type"] = "atclist";
    root["data"] = data;
    std::string finalJson = root.dump();

    if (force || finalJson != lastAtcListJson)
    {
        wsClient->SendMessage(finalJson);
        lastAtcListJson = finalJson;
    }
}
