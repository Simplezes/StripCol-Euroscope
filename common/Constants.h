#pragma once

#include <string>

namespace StripColConstants {
    const std::string PLUGIN_NAME = "StripCol";
    const std::string PLUGIN_VERSION = "2.7";
    const std::string PLUGIN_AUTHOR = "Simplezes";
    const std::string PLUGIN_COPYRIGHT = "Copyright 2026";

    const std::string DEFAULT_GATEWAY = "127.0.0.1";
    const std::string DEFAULT_PORT = "3000";
    
    const int WEBSOCKET_PING_INTERVAL_SEC = 30;
    const int ATCLIST_SYNC_INTERVAL_TICKS = 30;
}
