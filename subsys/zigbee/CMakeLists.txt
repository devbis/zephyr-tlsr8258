# SPDX-License-Identifier: Apache-2.0

if(CONFIG_ZIGBEE)

# Only expose the public Zephyr-facing include dir globally.
# SDK-internal dirs (common, os, mac/includes, etc.) are library-private
# to avoid shadowing standard headers (e.g. common/string.h vs <string.h>).
zephyr_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

zephyr_library_named(subsys__zigbee)

if(CONFIG_ZIGBEE_ROUTER)
    set(ZIGBEE_ROLE_DEFS
        ZB_ROUTER_ROLE=1
        ROUTER=1
        END_DEVICE=0
        COORDINATOR=0)
else()
    set(ZIGBEE_ROLE_DEFS
        ZB_ED_ROLE=1
        END_DEVICE=1
        ROUTER=0
        COORDINATOR=0)
endif()

zephyr_library_compile_definitions(
    ${ZIGBEE_ROLE_DEFS}
    NV_ENABLE=1
    SECURITY_ENABLE=1
    DEFAULT_CHANNEL=${CONFIG_ZIGBEE_CHANNEL}
    ZCL_REPORTING_TABLE_NUM=2
)

# Experimental: the ED built on the full libzigbee core (CONFIG_ZIGBEE_ED_LIBZIGBEE)
# compiles the unified vendor MAC/NWK/APS/SS/ZDO sources (role ZB_ED_ROLE). Several
# of those sources' declarations / whole-file bodies were guarded behind
# ZB_ROUTER_ROLE while the port was router-only; this marker lets those guards
# admit the ED-libzigbee build too, without perturbing the router or the default
# minimal-ED build.
zephyr_library_compile_definitions_ifdef(CONFIG_ZIGBEE_ED_LIBZIGBEE
    ZB_ED_ROLE_LIBZIGBEE=1
)

# Library-private include dirs — not visible to the rest of the build
zephyr_library_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/zephyr
    ${CMAKE_CURRENT_SOURCE_DIR}/os
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/mac/includes
    ${CMAKE_CURRENT_SOURCE_DIR}/zbapi
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Platform / OS abstraction
zephyr_library_sources(
    platform/zephyr/ev_buffer_zephyr.c
    platform/zephyr/ev_queue_zephyr.c
    platform/zephyr/ev_timer_zephyr.c
    platform/zephyr/ev_poll_zephyr.c
    platform/zephyr/zb_task_queue_zephyr.c
    platform/zephyr/zb_main.c
    platform/zephyr/drv_hw_zephyr.c
    platform/zephyr/drv_radio_map.c
    platform/zephyr/drv_radio_zephyr.c
    platform/zephyr/drv_nv_zephyr.c
    platform/zephyr/drv_security_zephyr.c
    platform/zephyr/zb_vendor_compat.c
    platform/zephyr/zb_persistence_zephyr.c
    platform/zephyr/zb_primitive_dispatch.c
)
if(NOT CONFIG_ENTROPY_DEVICE_RANDOM_GENERATOR
   AND NOT CONFIG_XOSHIRO_RANDOM_GENERATOR
   AND NOT CONFIG_TIMER_RANDOM_GENERATOR
   AND NOT CONFIG_ARM64_RANDOM_GENERATOR)
    zephyr_library_sources(platform/zephyr/drv_rand_zephyr.c)
endif()
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_RADIO_PORT_TLSR8258
    platform/zephyr/zb_radio_port_tlsr8258.c
)
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_RADIO_PORT_NATIVE_SIM_SOCKET
    platform/common/zb_native_sim_socket_medium.c
    platform/zephyr/zb_radio_port_native_sim_socket.c
)

# Common utilities
zephyr_library_sources(
    common/list.c
    common/mempool.c
    common/ringbuffer.c
    common/zb_minimal_ccm.c
    common/utility.c
    common/zb_config.c
)

# MAC layer
zephyr_library_sources(
    mac/mac_phy.c
    mac/mac_pib.c
)
# The full libzigbee MAC (mac/mac.c, mac/mac_trx.c, ...) is used by the router
# and by the experimental libzigbee-based ED build. The minimal ED path
# (mac_trx_compat / mac_trx_stub) is used only by the default (minimal) ED build.
if(NOT CONFIG_ZIGBEE_ROUTER AND NOT CONFIG_ZIGBEE_ED_LIBZIGBEE)
    zephyr_library_sources_ifndef(CONFIG_ZIGBEE_MAC_TRX_STUBS mac/mac_trx_compat.c)
    zephyr_library_sources_ifdef(CONFIG_ZIGBEE_MAC_TRX_STUBS mac/mac_trx_stub.c)
endif()

# Upper layers (open-source SDK files)
zephyr_library_sources(
    af/zb_af.c
    aps/aps_group.c
    ss/ss_nv.c
    zbapi/zb_api_ed_compat.c
    nwk/nwk_schedule_fallback.c
)

# Full libzigbee core — compiled for the router AND the experimental
# libzigbee-based ED build (CONFIG_ZIGBEE_ED_LIBZIGBEE). These are the unified
# vendor sources whose role behaviour is selected internally by
# ZB_ED_ROLE / ZB_ROUTER_ROLE, so the same files serve both roles.
if(CONFIG_ZIGBEE_ROUTER OR CONFIG_ZIGBEE_ED_LIBZIGBEE)
    zephyr_library_sources(
        aps/aps.c
        aps/aps_data.c
        aps/aps_me.c
        zdo/zdo.c
        zdo/zdo_nwk_manager.c
        zdo/zdp_services.c
        ss/ss_apsEnDecrypt.c
        ss/ss_apsSecurityME.c
        ss/ss_nwkEnDecrypt.c
        ss/ss_tlCCM.c
        ss/ss_zdoSecurityME.c
        nwk/nwk.c
        nwk/nwk_data.c
        nwk/nwk_join.c
        nwk/nwk_nlme.c
        nwk/nwk_neighbor.c
        nwk/nwk_discovery.c
        nwk/nwk_route_disc.c
        nwk/nwk_routing.c
        nwk/nwk_brc.c
        nwk/nwk_addr_map.c
        nwk/nwk_addr_conflict.c
        nwk/nwk_panid_conflict.c
        nwk/nwk_leave.c
        nwk/nwk_endDev_timeout.c
        nwk/nwk_pend.c
        nwk/nwk_formation.c
        nwk/nwk_permit_joining.c
        nwk/nwk_address_assign.c
        mac/mac.c
        mac/mac_associate.c
        mac/mac_common.c
        mac/mac_data.c
        mac/mac_mlme.c
        mac/mac_scan.c
        mac/mac_test.c
        mac/mac_trx.c
        mac/mac_cr_coordinator.c
        mac/mac_indirect_data.c
        common/zb_initialize.c
        platform/zephyr/zb_buffer_zephyr.c
        platform/zephyr/zb_task_queue_router.c
        platform/zephyr/zb_second_clock.c
    )
endif()

zephyr_library_sources_ifdef(CONFIG_ZIGBEE_BDB bdb/bdb.c)
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_BDB platform/zephyr/zb_bdb_bootstrap.c)
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_OTA ota/ota.c ota/otaEpCfg.c)
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_GP
    gp/gp.c gp/gp_proxy.c gp/gp_sink.c
    gp/gp_proxyTab.c gp/gp_sinkTab.c
    gp/gp_trans.c gp/gp_transCfg.c
    gp/gpEpCfg.c)
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_WWAH wwah/wwah.c wwah/wwahEpCfg.c)

# ZCL
add_subdirectory_ifdef(CONFIG_ZIGBEE_ZCL zcl)

# ED minimal runtime — the DEFAULT ED path. Superseded by the full libzigbee
# core (above) for the experimental CONFIG_ZIGBEE_ED_LIBZIGBEE build.
if(CONFIG_ZIGBEE_ED AND NOT CONFIG_ZIGBEE_ED_LIBZIGBEE)
    zephyr_library_sources(
        zbapi/zb_api_zdo_send_minimal.c
        nwk/nwk_ed_minimal.c
        zdo/zdo_ed_minimal.c
    )
endif()
# Phase-2 placeholder glue for the experimental libzigbee-based ED: inert stubs
# for the minimal-ED API the platform/app layer still calls, so the image links.
# Phase-3 rewires the callers to the real libzigbee ED path and deletes this.
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_ED_LIBZIGBEE
    zbapi/zb_ed_libzigbee_stubs.c
)
if(CONFIG_ZIGBEE_ED AND CONFIG_ZIGBEE_BDB)
    zephyr_library_sources(zbapi/zb_api_bdb_ed_compat.c)
endif()

# Router-only Zephyr-port bootstrap glue (not part of the vendor libzigbee core;
# these provide router formation/parenting bring-up and the router af_dataSend).
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_ROUTER
    nwk/nwk_router_minimal.c
    zdo/zdo_router_minimal.c
)
if(CONFIG_ZIGBEE_ROUTER AND CONFIG_ZIGBEE_BDB)
    zephyr_library_sources(zbapi/zb_api_bdb_ed_compat.c)
endif()

endif() # CONFIG_ZIGBEE
