#include "pch.h"
#include "plugin/StripCol.h"

namespace
{
    StripCol *g_plugin = nullptr;
}

void EuroScopePlugInInit(EuroScopePlugIn::CPlugIn **ppPlugInInstance)
{
    g_plugin = new StripCol();
    *ppPlugInInstance = g_plugin;
}

void EuroScopePlugInExit()
{
    delete g_plugin;
    g_plugin = nullptr;
}
