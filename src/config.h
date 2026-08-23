#pragma once

namespace config
{
    inline constexpr bool minimal_proxy = false;
    #ifdef MW169_PROXY_ONLY
    inline constexpr bool proxy_only = true;
    #else
    inline constexpr bool proxy_only = false;
    #endif
    inline constexpr bool show_console = true;
    inline constexpr bool trace_dvars = true;

    // Startup is staged. Keep the proxy footprint small until a live launch proves
    // that the retail process survives the proxy alone.
    // Stage one: force the two verified offline registration candidates.
    // The LoadImageA trigger waits for matching retail prologues.
    inline constexpr bool hook_dvar_bool = true;
    inline constexpr bool hook_dvar_variant = true;
    inline constexpr bool install_load_image_trigger = true;
    // Arm the CWHook anti-tamper base from the startup worker, after loader
    // lock work is complete. It does not scan or patch the game text at this
    // stage. Checksum healing waits for the verified LoadImageA edge.
    inline constexpr bool install_cwhook = true;
    // The broad CWHook window/thread API layer is not required by the
    // checksum healer. Keep it off for 1.69 until each target is verified.
    inline constexpr bool install_cwhook_system_hooks = false;
    // CWHook's first-position VEH observes every retail exception. The 1.69
    // startup loop at game+0xB1EE64 shows that this path is not safe yet.
    inline constexpr bool install_cwhook_veh = false;
    // Keep the broad sign-in getter hook disabled. The retail boot path uses
    // this predicate to decide whether it should start Blizzard login.
    inline constexpr bool hook_signed_in_getter = false;
    inline constexpr bool hook_sign_in_fence = true;
    // These are the two retail LUI gates used by the 1.64 menu scripts.
    // Keep the hooks narrow. Do not change the global menu-mode belief at boot.
    inline constexpr bool hook_lui_mode_ownership = true;
    inline constexpr bool hook_lui_premium_gate = true;
    inline constexpr bool hook_lui_offline_ownership = true;
    inline constexpr bool hook_online_service_fence = true;
    inline constexpr bool hook_online_data_fence = true;
    // This binding feeds the Battle.net connection row in the online boot menu.
    // It reports the local community state. It does not start a service.
    inline constexpr bool hook_battle_net_connected_fence = true;
    // FencePlatformServices.lua expects Engine.BAAABFICDA to return 1 when
    // local Battle.net services are ready.
    inline constexpr bool hook_platform_services_state = true;
    // FenceExchange.lua treats Commerce.CIIJICHHF values 2 and above as
    // complete. Report the local exchange state as complete.
    inline constexpr bool hook_local_exchange_state = true;
    // Report MP content as installed only when the local retail MP base files
    // exist and local content enumeration has completed.
    // Keep the content binding as an observation point. Do not report a fake
    // installed state until the retail player-data contexts are ready.
    inline constexpr bool hook_local_mp_content_state = true;
    inline constexpr bool force_local_mp_content_state = true;
    // The first DDL pass only records the retail call seam. Enable the seed
    // after its runtime bytes match the 1.69 investigation notes.
    inline constexpr bool seed_local_ddl = false;

    // This scanner reads retail data tables. It does not call the Lua chunk
    // executor or parser. The narrow registration override below changes only
    // copied LUI function pointers before the retail registration call.
    inline constexpr bool dump_lui_bindings = true;
    // Capture Lua registration tables through the retail registration API.
    inline constexpr bool dump_lui_registrations = true;
    // Report local content state through four exact Engine bindings. The
    // override is data-only. The verified luaL_openlib hook remains the only
    // code hook in this path.
    inline constexpr bool override_local_content_bindings = true;
    // Keep the retail patch fence local. Its start binding launches the patch
    // query, and its state binding reports the local success state.
    inline constexpr bool hook_patch_query_fence = true;
    inline constexpr bool hook_patch_state_fence = true;
    // Report only the two local profile-completion flags used by the account
    // summary fence. Keep the general profile getter unchanged.
    inline constexpr bool hook_profile_account_flags = true;
    // Replace only the retail MP menu chunks that access absent offline
    // playlists. The replacement keeps the local Private Match/Trials item.
    inline constexpr bool hook_lua_mp_menu = true;
    // Capture LuaJIT errors without re-entering the VM. This includes first-
    // chance Lua error exceptions, panic messages, and failed local chunks.
    inline constexpr bool hook_lua_errors = true;
    inline constexpr bool block_public_network_as_backup = false;
    inline constexpr bool preload_original_discord = false;
    inline constexpr bool forward_discord = true;
    inline constexpr bool crash_diagnostics = true;
}
