#pragma once

#include "EuroScopePlugIn.h"
#include "flightplan/ProcedureCatalog.h"
#include "gateway/GatewayLink.h"
#include "plugin/ClearanceScreen.h"
#include "sector/SectorParser.h"
#include "sync/AircraftTracker.h"
#include "sync/AtcListSync.h"
#include <string>
#include <vector>

class StripCol : public EuroScopePlugIn::CPlugIn
{
public:
    StripCol();
    ~StripCol() override = default;

    void OnTimer(int Counter) override;

    void OnControllerPositionUpdate(EuroScopePlugIn::CController Controller) override;
    void OnControllerDisconnect(EuroScopePlugIn::CController Controller) override;
    void OnFlightPlanFlightPlanDataUpdate(EuroScopePlugIn::CFlightPlan FlightPlan) override;
    void OnFlightPlanControllerAssignedDataUpdate(EuroScopePlugIn::CFlightPlan FlightPlan, int DataType) override;
    void OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan) override;
    bool OnCompileCommand(const char *sCommandLine) override;
    EuroScopePlugIn::CRadarScreen *OnRadarScreenCreated(const char *sDisplayName, bool NeedRadarContent,
                                                        bool GeoReferenced, bool CanBeSaved,
                                                        bool CanBeCreated) override;

private:
    void StartSession(bool newPairingCode);
    void StopSession();
    void Notify(const char *category, const std::string &text);

    void RunSetGatewayCommand(const std::string &argument);
    void RunRestartCommand();
    void RunPairingCodeCommand();
    void RunDebugConsoleCommand();
    void RunSectorFileCommand(const std::string &argument);
    void LoadSectorData();
    void SendSectorData();

    ProcedureCatalog catalog_;
    GatewayLink link_;
    AircraftTracker tracker_;
    AtcListSync atcList_;
    std::vector<ClearanceScreen *> screens_;
    std::string sectorMessage_;
    std::string sectorPath_;
};
