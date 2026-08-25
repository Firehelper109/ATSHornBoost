#include "BoostBackend.h"

BoostBackendResult TickBoostBackend(const PluginState& state, const HornBoostConfig& config, float delta_seconds)
{
    (void)state;
    (void)config;
    (void)delta_seconds;

    // Reserved for a future verified vehicle-physics write path.
    // The current release implements boost through the official SCS Input SDK.
    return {};
}
