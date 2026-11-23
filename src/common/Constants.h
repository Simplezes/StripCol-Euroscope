#pragma once

namespace StripColConstants
{
    constexpr const char *PLUGIN_NAME = "StripCol";
    constexpr const char *PLUGIN_VERSION = "3.0";
    constexpr const char *PLUGIN_AUTHOR = "Simplezes";
    constexpr const char *PLUGIN_COPYRIGHT = "Copyright 2026";

    constexpr const char *DEFAULT_GATEWAY = "127.0.0.1";
    constexpr const char *GATEWAY_PORT = "3000";
    constexpr const char *PROCEDURES_FILE = "data\\procedures.json";

    constexpr int PING_INTERVAL_SECONDS = 15;
    constexpr int INBOUND_SILENCE_LIMIT_SECONDS = 45;
    constexpr int CONNECT_TIMEOUT_MS = 3000;
    constexpr int HANDSHAKE_TIMEOUT_MS = 5000;
    constexpr int SEND_TIMEOUT_MS = 10000;
    constexpr int RECONNECT_DELAY_MIN_MS = 1000;
    constexpr int RECONNECT_DELAY_MAX_MS = 10000;
    constexpr unsigned MAX_MESSAGE_BYTES = 1024 * 1024;
    constexpr unsigned MAX_QUEUED_MESSAGES = 2048;

    constexpr int ATC_LIST_INTERVAL_TICKS = 15;
    constexpr int AIRCRAFT_REFRESH_TICKS = 5;

    constexpr int MAX_ASSIGNED_SPEED = 500;
    constexpr int MAX_HEADING = 360;
    constexpr double MAX_ASSIGNED_MACH = 3.0;
    constexpr double MACH_SCALE = 100.0;
    constexpr double NEARBY_AIRCRAFT_RANGE_NM = 50.0;
    constexpr int FLIGHT_STRIP_ANNOTATION_COUNT = 9;
}
