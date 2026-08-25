#pragma once

#include "Config.h"
#include "PluginState.h"

struct BoostBackendResult
{
    bool backend_available = false;
    bool force_applied = false;
};

BoostBackendResult TickBoostBackend(const PluginState& state, const HornBoostConfig& config, float delta_seconds);
