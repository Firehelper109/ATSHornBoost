#include "Config.h"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
float ReadFloat(const std::string& path, const char* section, const char* key, float default_value)
{
#ifdef _WIN32
    char default_text[64]{};
    char value[64]{};
    sprintf_s(default_text, "%g", default_value);
    GetPrivateProfileStringA(section, key, default_text, value, static_cast<DWORD>(sizeof(value)), path.c_str());
    return std::strtof(value, nullptr);
#else
    (void)path; (void)section; (void)key;
    return default_value;
#endif
}

int ReadInt(const std::string& path, const char* section, const char* key, int default_value)
{
#ifdef _WIN32
    return static_cast<int>(GetPrivateProfileIntA(section, key, default_value, path.c_str()));
#else
    (void)path; (void)section; (void)key;
    return default_value;
#endif
}
}

HornBoostConfig LoadConfig(const std::string& ini_path)
{
    HornBoostConfig cfg;
    cfg.trigger_virtual_key = ReadInt(ini_path, "input", "trigger_virtual_key", cfg.trigger_virtual_key);
    cfg.drive_virtual_key = ReadInt(ini_path, "input", "drive_virtual_key", cfg.drive_virtual_key);
    cfg.require_game_foreground = ReadInt(ini_path, "input", "require_game_foreground", cfg.require_game_foreground ? 1 : 0) != 0;

    cfg.normal_throttle = ReadFloat(ini_path, "drive", "normal_throttle", cfg.normal_throttle);
    cfg.semantic_throttle = ReadFloat(ini_path, "boost", "semantic_throttle", cfg.semantic_throttle);
    cfg.boost_strength = ReadFloat(ini_path, "boost", "strength", cfg.boost_strength);
    cfg.verbose_telemetry = ReadInt(ini_path, "debug", "verbose_telemetry", cfg.verbose_telemetry ? 1 : 0) != 0;
    return cfg;
}
