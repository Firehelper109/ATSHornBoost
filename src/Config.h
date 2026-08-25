#pragma once

#include <string>

struct HornBoostConfig
{
    int trigger_virtual_key = 0x48; // H: horn / boost
    int drive_virtual_key = 0x57;   // W: normal acceleration handled by plugin

    float normal_throttle = 0.10f;
    float semantic_throttle = 1.0f;
    float boost_strength = 25.0f;

    bool require_game_foreground = true;
    bool verbose_telemetry = false;
};

HornBoostConfig LoadConfig(const std::string& ini_path);
