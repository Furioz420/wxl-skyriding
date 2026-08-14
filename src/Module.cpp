#include "ExtensionApi.hpp"

const WXL_PluginInfo* __cdecl WXL_Query(void)
{
    static const WXL_PluginInfo info{
        sizeof(WXL_PluginInfo), WXL_API_VERSION, "wxl-skyriding", 1, WXL_CLIENT_BUILD,
    };
    return &info;
}

int __cdecl WXL_Load(const WXL_Api* api)
{
    if (!api || api->apiVersion != WXL_API_VERSION) return 0;
    wxl_skyriding::g_api = api;

    if (!wxl_skyriding::ConfigBool("WXL_SKYRIDING", true))
    {
        api->Log(WXL_LOG_INFO, "wxl-skyriding", "controller disabled by configuration");
        return 1;
    }
    if (!wxl_skyriding::Network())
    {
        api->Log(WXL_LOG_ERROR, "wxl-skyriding", "required wxl.network v1 is unavailable");
        return 0;
    }
    if (!wxl_skyriding::Animation())
    {
        api->Log(WXL_LOG_ERROR, "wxl-skyriding", "required wxl.m2-animation v1 is unavailable");
        return 0;
    }
    return wxl_skyriding::InstallSkyriding() ? 1 : 0;
}
