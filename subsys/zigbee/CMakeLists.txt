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
zephyr_library_sources_ifndef(CONFIG_ZIGBEE_MAC_TRX_STUBS mac/mac_trx_compat.c)
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_MAC_TRX_STUBS mac/mac_trx_stub.c)

# Upper layers (open-source SDK files)
zephyr_library_sources(
    af/zb_af.c
    aps/aps_group.c
    ss/ss_nv.c
)

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

# Role-specific runtime sources.
# ED runtime is the fully wired path. Router runtime is being added
# incrementally and currently shares the ED minimal infrastructure
# (steering/join helpers) plus a router-specific bootstrap stub.
zephyr_library_sources_ifdef(CONFIG_ZIGBEE_ED
    zbapi/zb_api_ed_compat.c
    zbapi/zb_api_zdo_send_minimal.c
    nwk/nwk_schedule_fallback.c
    nwk/nwk_ed_minimal.c
    zdo/zdo_ed_minimal.c
)
if(CONFIG_ZIGBEE_ED AND CONFIG_ZIGBEE_BDB)
    zephyr_library_sources(zbapi/zb_api_bdb_ed_compat.c)
endif()

zephyr_library_sources_ifdef(CONFIG_ZIGBEE_ROUTER
    zbapi/zb_api_ed_compat.c
    zbapi/zb_api_zdo_send_minimal.c
    nwk/nwk_schedule_fallback.c
    nwk/nwk_ed_minimal.c
    nwk/nwk_router_minimal.c
    nwk/nwk_permit_joining.c
    nwk/nwk_routing.c
    nwk/nwk_brc.c
    nwk/nwk_neighbor.c
    nwk/nwk_addr_map.c
    nwk/nwk_address_assign.c
    nwk/nwk_formation.c
    zdo/zdo_ed_minimal.c
    zdo/zdo_router_minimal.c
)
if(CONFIG_ZIGBEE_ROUTER AND CONFIG_ZIGBEE_BDB)
    zephyr_library_sources(zbapi/zb_api_bdb_ed_compat.c)
endif()

endif() # CONFIG_ZIGBEE
