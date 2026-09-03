# SPDX-License-Identifier: Apache-2.0

if(CONFIG_ZIGBEE)

# Only expose the public Zephyr-facing include dir globally.
# SDK-internal dirs (common, os, mac/includes, etc.) are library-private
# to avoid shadowing standard headers (e.g. common/string.h vs <string.h>).
zephyr_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

zephyr_library_named(subsys__zigbee)

if(CONFIG_ZIGBEE_COORDINATOR)
    set(ZIGBEE_ROLE_DEFS
        ZB_COORDINATOR_ROLE=1
        ZB_ROUTER_ROLE=1
        END_DEVICE=0
        ROUTER=0
        COORDINATOR=1)
elseif(CONFIG_ZIGBEE_ROUTER)
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


# Library-private include dirs — not visible to the rest of the build
zephyr_library_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/zephyr
    ${CMAKE_CURRENT_SOURCE_DIR}/os
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/ss
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
    platform/zephyr/zb_persistence_zephyr.c
    platform/zephyr/zb_primitive_dispatch.c
)

# Use the vendor TLSR8258 AES implementation for the libzigbee security
# service.  The archive's aes.o is the exact hardware implementation used by
# the working vendor router; keep it out of native_sim and other targets.
if(CONFIG_ZIGBEE_RADIO_PORT_TLSR8258)
    zephyr_link_libraries(${ZEPHYR_BASE}/../drivers/libdrivers_8258.a)
endif()

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
# The full libzigbee MAC (mac/mac.c, mac/mac_trx.c, ...) is used by both roles;
# the former minimal-ED MAC glue has been removed.

# Upper layers (open-source SDK files)
zephyr_library_sources(
    af/zb_af.c
    aps/aps_group.c
    ss/ss_nv.c
    zbapi/zb_api.c
    nwk/nwk_schedule_fallback.c
)

# Full libzigbee core — the single unified stack for BOTH roles (router and ED).
# These are the unified vendor sources whose role behaviour is selected internally
# by ZB_ED_ROLE / ZB_ROUTER_ROLE, so the same files serve both roles.
if(CONFIG_ZIGBEE_COORDINATOR OR CONFIG_ZIGBEE_ROUTER OR CONFIG_ZIGBEE_ED)
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

zephyr_library_sources_ifdef(CONFIG_ZIGBEE_BDB bdb/bdb.c bdb/bdb_base.c)
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_BDB platform/zephyr/zb_bdb_bootstrap.c)
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_OTA ota/ota.c ota/otaEpCfg.c)
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_GP
    gp/cGP_stub.c gp/dGP_stub.c
    gp/gp_base.c gp/gp_sec.c
    gp/gp.c gp/gp_proxy.c gp/gp_sink.c
    gp/gp_proxyTab.c gp/gp_sinkTab.c
    gp/gp_trans.c gp/gp_transCfg.c
    gp/gpEpCfg.c)
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_WWAH wwah/wwah.c wwah/wwahEpCfg.c)

# ZCL
add_subdirectory_ifdef(CONFIG_ZIGBEE_ZCL zcl)

# ED-specific Zephyr persistence/bootstrap hooks.  Join, rejoin, polling and
# ZDO requests themselves are implemented by the vendor-derived NWK/ZDO/API
# sources above; this file only contains the small ED platform seam.
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_ED
    nwk/nwk_ed_libzigbee.c
)

# FFD Zephyr-port bootstrap glue (formation/parenting bring-up).
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_ROUTER
    nwk/nwk_router_bootstrap.c
)
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_COORDINATOR
    nwk/nwk_router_bootstrap.c
)
# Role-agnostic Zephyr-port glue: af_dataSend (outbound ZDO/ZCL APS -> NLDE) and
# the zdo_cfg_attributes SYS_INIT that network discovery needs (non-zero scan
# attempts/duration). Shared by all full-stack roles.
if(CONFIG_ZIGBEE_COORDINATOR OR CONFIG_ZIGBEE_ROUTER OR CONFIG_ZIGBEE_ED)
    zephyr_library_sources(zdo/zdo_zephyr_glue.c)
endif()
endif() # CONFIG_ZIGBEE
