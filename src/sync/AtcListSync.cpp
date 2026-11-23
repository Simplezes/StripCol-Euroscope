#include "pch.h"
#include "sync/AtcListSync.h"
#include "common/Constants.h"
#include "common/Text.h"
#include "gateway/JsonFields.h"
#include <nlohmann/json.hpp>

using namespace EuroScopePlugIn;
using json = nlohmann::json;

AtcListSync::AtcListSync(CPlugIn &plugin, GatewayLink &link) : plugin_(plugin), link_(link)
{
}

void AtcListSync::Invalidate()
{
    forced_ = true;
}

std::string AtcListSync::Build()
{
    json controllers = json::array();
    for (CController controller = plugin_.ControllerSelectFirst(); controller.IsValid();
         controller = plugin_.ControllerSelectNext(controller))
    {
        json entry;
        entry["callsign"] = Text::FromCString(controller.GetCallsign());
        entry["positionId"] = Text::FromCString(controller.GetPositionId());
        entry["fullName"] = Text::FromCString(controller.GetFullName());
        entry["rating"] = controller.GetRating();
        entry["frequency"] = Text::FormatFrequency(controller.GetPrimaryFrequency());
        controllers.push_back(std::move(entry));
    }

    json message;
    message["type"] = "atclist";
    message["data"] = std::move(controllers);
    return JsonFields::Dump(message);
}

void AtcListSync::Update(int tick)
{
    if (!link_.IsConnected())
        return;
    if (!forced_ && tick % StripColConstants::ATC_LIST_INTERVAL_TICKS != 0)
        return;

    std::string current = Build();
    if (!forced_ && current == lastSent_)
        return;

    if (link_.Send(current))
    {
        lastSent_ = std::move(current);
        forced_ = false;
    }
}
