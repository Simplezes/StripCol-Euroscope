#include "pch.h"
#include "plugin/StripCol.h"

using namespace EuroScopePlugIn;

static StripCol *g_pPlugin = nullptr;

void EuroScopePlugInInit(CPlugIn **ppPlugInInstance)
{
    g_pPlugin = new StripCol();
    *ppPlugInInstance = g_pPlugin;
}

void EuroScopePlugInExit()
{
    delete g_pPlugin;
    g_pPlugin = nullptr;
}
