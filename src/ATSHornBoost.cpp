#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "scssdk_telemetry.h"
#include "scssdk_input.h"
#include "amtrucks/scssdk_input_ats.h"
#include "amtrucks/scssdk_ats.h"
#include "amtrucks/scssdk_telemetry_ats.h"

#include "BoostBackend.h"
#include "Config.h"
#include "PluginState.h"

namespace
{
PluginState g_state;
HornBoostConfig g_config;
scs_log_t g_game_log = nullptr;

#ifdef _WIN32
HMODULE g_module = nullptr;
#endif

std::chrono::steady_clock::time_point g_last_frame_time{};
std::chrono::steady_clock::time_point g_last_verbose_log{};
bool g_config_loaded = false;
bool g_telemetry_initialized = false;
bool g_input_initialized = false;
bool g_input_device_active = false;
unsigned g_next_input_event = 0;
bool g_drive_last_frame = false;

void GameLog(const scs_log_type_t type, const char* message)
{
    if (!g_game_log || !message) {
        return;
    }

    char buffer[1024]{};
#ifdef _MSC_VER
    sprintf_s(buffer, "[ATSHornBoost] %s", message);
#else
    snprintf(buffer, sizeof(buffer), "[ATSHornBoost] %s", message);
#endif
    g_game_log(type, buffer);
}

std::string ModuleDirectory()
{
#ifdef _WIN32
    char path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(g_module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return ".";
    }

    std::string result(path, length);
    const std::string::size_type slash = result.find_last_of("\\/");
    return slash == std::string::npos ? "." : result.substr(0, slash);
#else
    return ".";
#endif
}

void EnsureConfigLoaded()
{
    if (g_config_loaded) {
        return;
    }

    const std::string ini_path = ModuleDirectory() + "\\ATSHornBoost.ini";
    g_config = LoadConfig(ini_path);
    g_config_loaded = true;
}

bool IsGameForeground()
{
#ifdef _WIN32
    if (!g_config.require_game_foreground) {
        return true;
    }

    const HWND foreground = GetForegroundWindow();
    if (!foreground) {
        return false;
    }

    DWORD foreground_pid = 0;
    GetWindowThreadProcessId(foreground, &foreground_pid);
    return foreground_pid == GetCurrentProcessId();
#else
    return true;
#endif
}

bool IsVirtualKeyDown(const int virtual_key)
{
#ifdef _WIN32
    if (!IsGameForeground()) {
        return false;
    }
    return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
#else
    (void)virtual_key;
    return false;
#endif
}

bool IsBoostTriggerDown()
{
    return IsVirtualKeyDown(g_config.trigger_virtual_key);
}

bool IsDriveTriggerDown()
{
    return IsVirtualKeyDown(g_config.drive_virtual_key);
}

SCSAPI_VOID StoreFloat(const scs_string_t name, const scs_u32_t index, const scs_value_t* const value, const scs_context_t context)
{
    (void)name;
    (void)index;
    if (!value || !context || value->type != SCS_VALUE_TYPE_float) {
        return;
    }
    *static_cast<float*>(context) = value->value_float.value;
}

SCSAPI_VOID StoreS32(const scs_string_t name, const scs_u32_t index, const scs_value_t* const value, const scs_context_t context)
{
    (void)name;
    (void)index;
    if (!value || !context || value->type != SCS_VALUE_TYPE_s32) {
        return;
    }
    *static_cast<scs_s32_t*>(context) = value->value_s32.value;
}

struct OptionalFVectorTarget
{
    bool* available;
    scs_value_fvector_t* value;
};

SCSAPI_VOID StoreOptionalFVector(const scs_string_t name, const scs_u32_t index, const scs_value_t* const value, const scs_context_t context)
{
    (void)name;
    (void)index;
    if (!context) {
        return;
    }

    auto* target = static_cast<OptionalFVectorTarget*>(context);
    if (!value || value->type != SCS_VALUE_TYPE_fvector) {
        *target->available = false;
        return;
    }

    *target->available = true;
    *target->value = value->value_fvector;
}

struct OptionalEulerTarget
{
    bool* available;
    scs_value_euler_t* value;
};

SCSAPI_VOID StoreOptionalEuler(const scs_string_t name, const scs_u32_t index, const scs_value_t* const value, const scs_context_t context)
{
    (void)name;
    (void)index;
    if (!context) {
        return;
    }

    auto* target = static_cast<OptionalEulerTarget*>(context);
    if (!value || value->type != SCS_VALUE_TYPE_euler) {
        *target->available = false;
        return;
    }

    *target->available = true;
    *target->value = value->value_euler;
}

OptionalFVectorTarget g_velocity_target{ &g_state.local_velocity_available, &g_state.local_velocity };
OptionalFVectorTarget g_acceleration_target{ &g_state.local_acceleration_available, &g_state.local_acceleration };
OptionalEulerTarget g_orientation_target{ &g_state.orientation_available, &g_state.orientation };

SCSAPI_VOID OnPause(const scs_event_t event, const void* const event_info, const scs_context_t context)
{
    (void)event_info;
    (void)context;

    g_state.paused = (event == SCS_TELEMETRY_EVENT_paused);
    GameLog(SCS_LOG_TYPE_message, g_state.paused ? "Telemetry paused." : "Telemetry started/unpaused.");
}

SCSAPI_VOID OnFrameEnd(const scs_event_t event, const void* const event_info, const scs_context_t context)
{
    (void)event;
    (void)event_info;
    (void)context;

    const auto now = std::chrono::steady_clock::now();
    float delta_seconds = 0.0f;
    if (g_last_frame_time.time_since_epoch().count() != 0) {
        delta_seconds = std::chrono::duration<float>(now - g_last_frame_time).count();
    }
    g_last_frame_time = now;

    const bool trigger_down = !g_state.paused && IsBoostTriggerDown();
    g_state.boost_requested = trigger_down;

    if (trigger_down != g_state.trigger_last_frame) {
        GameLog(SCS_LOG_TYPE_message, trigger_down ? "Boost trigger pressed." : "Boost trigger released.");
        g_state.trigger_last_frame = trigger_down;
    }

    const bool drive_down = !g_state.paused && IsDriveTriggerDown();
    if (drive_down != g_drive_last_frame) {
        GameLog(SCS_LOG_TYPE_message, drive_down ? "Normal drive key pressed." : "Normal drive key released.");
        g_drive_last_frame = drive_down;
    }

    (void)delta_seconds;

    if (g_config.verbose_telemetry) {
        if (g_last_verbose_log.time_since_epoch().count() == 0 ||
            std::chrono::duration_cast<std::chrono::seconds>(now - g_last_verbose_log).count() >= 2) {
            char line[512]{};
#ifdef _MSC_VER
            sprintf_s(line,
                "speed=%.2f m/s rpm=%.0f gear=%d throttle=%.2f effective=%.2f velocity=(%.2f, %.2f, %.2f)",
                g_state.speed_mps, g_state.engine_rpm, static_cast<int>(g_state.gear),
                g_state.input_throttle, g_state.effective_throttle,
                g_state.local_velocity.x, g_state.local_velocity.y, g_state.local_velocity.z);
#else
            snprintf(line, sizeof(line),
                "speed=%.2f m/s rpm=%.0f gear=%d throttle=%.2f effective=%.2f velocity=(%.2f, %.2f, %.2f)",
                g_state.speed_mps, g_state.engine_rpm, static_cast<int>(g_state.gear),
                g_state.input_throttle, g_state.effective_throttle,
                g_state.local_velocity.x, g_state.local_velocity.y, g_state.local_velocity.z);
#endif
            GameLog(SCS_LOG_TYPE_message, line);
            g_last_verbose_log = now;
        }
    }
}
}

extern "C" SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const params)
{
    if (version != SCS_TELEMETRY_VERSION_1_01 || !params) {
        return SCS_RESULT_unsupported;
    }

    const auto* version_params = static_cast<const scs_telemetry_init_params_v101_t*>(params);
    g_game_log = version_params->common.log;

    if (!version_params->common.game_id || std::strcmp(version_params->common.game_id, SCS_GAME_ID_ATS) != 0) {
        GameLog(SCS_LOG_TYPE_error, "This build is intended for American Truck Simulator.");
        g_game_log = nullptr;
        return SCS_RESULT_unsupported;
    }

    EnsureConfigLoaded();

    char startup[512]{};
#ifdef _MSC_VER
    sprintf_s(startup,
        "Initializing telemetry. SDK game version %u.%u, drive VK=0x%02X normal=%.2f, boost VK=0x%02X boost=%.2f.",
        SCS_GET_MAJOR_VERSION(version_params->common.game_version),
        SCS_GET_MINOR_VERSION(version_params->common.game_version),
        g_config.drive_virtual_key, g_config.normal_throttle,
        g_config.trigger_virtual_key, g_config.semantic_throttle);
#else
    snprintf(startup, sizeof(startup),
        "Initializing telemetry. SDK game version %u.%u, drive VK=0x%02X normal=%.2f, boost VK=0x%02X boost=%.2f.",
        SCS_GET_MAJOR_VERSION(version_params->common.game_version),
        SCS_GET_MINOR_VERSION(version_params->common.game_version),
        g_config.drive_virtual_key, g_config.normal_throttle,
        g_config.trigger_virtual_key, g_config.semantic_throttle);
#endif
    GameLog(SCS_LOG_TYPE_message, startup);

    const bool events_ok =
        version_params->register_for_event(SCS_TELEMETRY_EVENT_frame_end, OnFrameEnd, nullptr) == SCS_RESULT_ok &&
        version_params->register_for_event(SCS_TELEMETRY_EVENT_paused, OnPause, nullptr) == SCS_RESULT_ok &&
        version_params->register_for_event(SCS_TELEMETRY_EVENT_started, OnPause, nullptr) == SCS_RESULT_ok;

    if (!events_ok) {
        GameLog(SCS_LOG_TYPE_error, "Failed to register required telemetry events.");
        g_game_log = nullptr;
        return SCS_RESULT_generic_error;
    }

    version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_speed, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, StoreFloat, &g_state.speed_mps);
    version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, StoreFloat, &g_state.engine_rpm);
    version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_engine_gear, SCS_U32_NIL, SCS_VALUE_TYPE_s32, SCS_TELEMETRY_CHANNEL_FLAG_none, StoreS32, &g_state.gear);
    version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_input_throttle, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, StoreFloat, &g_state.input_throttle);
    version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, StoreFloat, &g_state.effective_throttle);
    version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_local_linear_velocity, SCS_U32_NIL, SCS_VALUE_TYPE_fvector, SCS_TELEMETRY_CHANNEL_FLAG_no_value, StoreOptionalFVector, &g_velocity_target);
    version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_local_linear_acceleration, SCS_U32_NIL, SCS_VALUE_TYPE_fvector, SCS_TELEMETRY_CHANNEL_FLAG_no_value, StoreOptionalFVector, &g_acceleration_target);
    version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, SCS_U32_NIL, SCS_VALUE_TYPE_euler, SCS_TELEMETRY_CHANNEL_FLAG_no_value, StoreOptionalEuler, &g_orientation_target);

    g_state = {};
    g_state.paused = true;
    g_last_frame_time = {};
    g_last_verbose_log = {};
    g_telemetry_initialized = true;

    GameLog(SCS_LOG_TYPE_message, "Telemetry initialization complete.");
    return SCS_RESULT_ok;
}

extern "C" SCSAPI_VOID scs_telemetry_shutdown(void)
{
    GameLog(SCS_LOG_TYPE_message, "Telemetry API shutting down.");
    g_telemetry_initialized = false;
    if (!g_input_initialized) {
        g_game_log = nullptr;
    }
}

SCSAPI_VOID OnInputActive(const scs_u8_t active, const scs_context_t context)
{
    (void)context;
    g_input_device_active = active != 0;
    GameLog(SCS_LOG_TYPE_message,
        g_input_device_active ? "Semantic input device activated." : "Semantic input device deactivated.");
}

SCSAPI_RESULT OnInputEvent(scs_input_event_t* const event_info, const scs_u32_t flags, const scs_context_t context)
{
    (void)context;
    if (!event_info) {
        return SCS_RESULT_invalid_parameter;
    }

    if (flags & SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame) {
        g_next_input_event = 0;
    }

    if (g_next_input_event > 0) {
        return SCS_RESULT_not_found;
    }

    EnsureConfigLoaded();

    const bool boost_down = !g_state.paused && IsBoostTriggerDown();
    const bool drive_down = !g_state.paused && IsDriveTriggerDown();
    const float requested = boost_down
        ? g_config.semantic_throttle
        : (drive_down ? g_config.normal_throttle : 0.0f);

    event_info->input_index = 0;
    event_info->value_float.value = std::clamp(requested, 0.0f, 1.0f);
    ++g_next_input_event;
    return SCS_RESULT_ok;
}

extern "C" SCSAPI_RESULT scs_input_init(const scs_u32_t version, const scs_input_init_params_t* const params)
{
    if (version != SCS_INPUT_VERSION_1_00 || !params) {
        return SCS_RESULT_unsupported;
    }

    const auto* version_params = static_cast<const scs_input_init_params_v100_t*>(params);
    g_game_log = version_params->common.log;

    if (!version_params->common.game_id || std::strcmp(version_params->common.game_id, SCS_GAME_ID_ATS) != 0) {
        GameLog(SCS_LOG_TYPE_error, "Input API: this build is intended for American Truck Simulator.");
        return SCS_RESULT_unsupported;
    }

    EnsureConfigLoaded();

    scs_input_device_input_t inputs[1]{};
    inputs[0].name = "aforward";
    inputs[0].display_name = "Horn Boost Throttle";
    inputs[0].value_type = SCS_VALUE_TYPE_float;

    scs_input_device_t device{};
    device.name = "ats_horn_boost";
    device.display_name = "ATS Horn Boost";
    device.type = SCS_INPUT_DEVICE_TYPE_semantical;
    device.input_count = 1;
    device.inputs = inputs;
    device.callback_context = nullptr;
    device.input_active_callback = OnInputActive;
    device.input_event_callback = OnInputEvent;

    const scs_result_t result = version_params->register_device(&device);
    if (result != SCS_RESULT_ok) {
        GameLog(SCS_LOG_TYPE_error, "Input API: failed to register semantic aforward device.");
        return SCS_RESULT_generic_error;
    }

    g_input_initialized = true;

    char line[256]{};
#ifdef _MSC_VER
    sprintf_s(line,
        "Input API initialized. aforward registered; drive VK=0x%02X normal=%.2f, boost VK=0x%02X boost=%.2f.",
        g_config.drive_virtual_key, g_config.normal_throttle,
        g_config.trigger_virtual_key, g_config.semantic_throttle);
#else
    snprintf(line, sizeof(line),
        "Input API initialized. aforward registered; drive VK=0x%02X normal=%.2f, boost VK=0x%02X boost=%.2f.",
        g_config.drive_virtual_key, g_config.normal_throttle,
        g_config.trigger_virtual_key, g_config.semantic_throttle);
#endif
    GameLog(SCS_LOG_TYPE_message, line);
    return SCS_RESULT_ok;
}

extern "C" SCSAPI_VOID scs_input_shutdown(void)
{
    GameLog(SCS_LOG_TYPE_message, "Input API shutting down.");
    g_input_initialized = false;
    g_input_device_active = false;
    if (!g_telemetry_initialized) {
        g_game_log = nullptr;
    }
}

#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
#endif
