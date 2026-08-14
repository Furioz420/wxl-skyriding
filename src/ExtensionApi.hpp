// wxl-skyriding extension-local access to the hub ABI and its shared services.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include "common/ExtensionConfig.hpp"
#include "wxl/M2AnimationApi.h"
#include "wxl/NetworkApi.h"
#include "wxl/PluginApi.h"

#include <cstddef>
#include <cstdint>

namespace wxl_skyriding
{
    extern const WXL_Api* g_api;
    extern const WXL_NetworkApi* g_network;
    extern const WXL_M2AnimationApi* g_animation;

    inline const WXL_NetworkApi* Network()
    {
        if (!g_network)
            g_network = static_cast<const WXL_NetworkApi*>(
                g_api->GetInterface("wxl.network", WXL_NETWORK_API_VERSION));
        return g_network;
    }

    inline const WXL_M2AnimationApi* Animation()
    {
        if (!g_animation)
            g_animation = static_cast<const WXL_M2AnimationApi*>(
                g_api->GetInterface("wxl.m2-animation", WXL_M2_ANIMATION_API_VERSION));
        return g_animation;
    }

    inline bool ConfigBool(const char* name, bool fallback)
    {
        char value[16] = {};
        return wxl::ext::config::Raw(name, value, sizeof value,
                                     "Extensions\\wxl-skyriding\\wxl-skyriding.cfg")
            ? wxl::ext::config::Truthy(value, fallback)
            : fallback;
    }

    bool InstallSkyriding();
}

#define WLOG_TRACE(...) ::wxl_skyriding::g_api->Log(WXL_LOG_TRACE, "wxl-skyriding", __VA_ARGS__)
#define WLOG_DEBUG(...) ::wxl_skyriding::g_api->Log(WXL_LOG_DEBUG, "wxl-skyriding", __VA_ARGS__)
#define WLOG_INFO(...)  ::wxl_skyriding::g_api->Log(WXL_LOG_INFO,  "wxl-skyriding", __VA_ARGS__)
#define WLOG_WARN(...)  ::wxl_skyriding::g_api->Log(WXL_LOG_WARN,  "wxl-skyriding", __VA_ARGS__)
#define WLOG_ERROR(...) ::wxl_skyriding::g_api->Log(WXL_LOG_ERROR, "wxl-skyriding", __VA_ARGS__)
