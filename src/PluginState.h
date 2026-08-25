#pragma once

#include "scssdk.h"
#include "scssdk_value.h"

struct PluginState
{
    bool paused = true;
    bool boost_requested = false;
    bool trigger_last_frame = false;

    float speed_mps = 0.0f;
    float engine_rpm = 0.0f;
    float input_throttle = 0.0f;
    float effective_throttle = 0.0f;
    scs_s32_t gear = 0;

    bool local_velocity_available = false;
    scs_value_fvector_t local_velocity{};

    bool local_acceleration_available = false;
    scs_value_fvector_t local_acceleration{};

    bool orientation_available = false;
    scs_value_euler_t orientation{};
};
