#pragma once

#include "EuroScopePlugIn.h"
#include "flightplan/ProcedureCatalog.h"
#include "gateway/GatewayLink.h"
#include "sector/SectorParser.h"
#include "sync/AircraftTracker.h"
#include "sync/AtcListSync.h"
#include <string>

constexpr int TAG_ITEM_SQUAWK = 102;
constexpr int TAG_FUNC_ASSIGN_SQUAWK = 102;

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

    void OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan,
                      EuroScopePlugIn::CRadarTarget RadarTarget,
                      int ItemCode,
                      int TagData,
                      char sItemString[16],
                      int *pColorCode,
                      COLORREF *pRGB,
                      double *pFontSize) override;

    void OnFunctionCall(int FunctionId,
                        const char *sItemString,
                        POINT Pt,
                        RECT Area) override;

private:
    void StartSession(bool newPairingCode);
    void StopSession();
    void Notify(const char *category, const std::string &text);

    void RunSetGatewayCommand(const std::string &argument);
    void RunRestartCommand();
    void RunPairingCodeCommand();
    void RunToggleSquawkCommand();
    void RunSectorFileCommand(const std::string &argument);
    void LoadSectorData();
    void SendSectorData();

    ProcedureCatalog catalog_;
    GatewayLink link_;
    AircraftTracker tracker_;
    AtcListSync atcList_;
    std::string sectorMessage_;
    std::string sectorPath_;
    bool squawkAssignmentEnabled_ = true;
};
