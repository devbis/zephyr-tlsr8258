# libzigbee port manifest

This file is the audit list for `../libzigbee/CMakeLists.txt` variable
`ZB_RECOVERED_SOURCES`.  Functional files in the list retain the vendor
source shape and Apache/Telink header where present.  Zephyr kernel, storage
and radio services are deliberately implemented by platform replacements.

| Vendor source | Zephyr implementation | Classification |
|---|---|---|
| `aps.c`, `aps_data.c`, `aps_me.c` | `subsys/zigbee/aps/*.c` | adapted vendor source |
| `mac.c`, `mac_associate.c`, `mac_common.c`, `mac_cr_coordinator.c`, `mac_data.c`, `mac_indirect_data.c`, `mac_mlme.c`, `mac_scan.c`, `mac_test.c`, `mac_trx.c` | `subsys/zigbee/mac/*.c` | adapted vendor source |
| `nwk.c`, `nwk_addr_conflict.c`, `nwk_addr_map.c`, `nwk_address_assign.c`, `nwk_brc.c`, `nwk_data.c`, `nwk_discovery.c`, `nwk_endDev_timeout.c`, `nwk_formation.c`, `nwk_join.c`, `nwk_leave.c`, `nwk_neighbor.c`, `nwk_nlme.c`, `nwk_panid_conflict.c`, `nwk_pend.c`, `nwk_permit_joining.c`, `nwk_route_disc.c`, `nwk_routing.c` | `subsys/zigbee/nwk/*.c` | adapted vendor source |
| `zdo.c`, `zdo_nwk_manager.c`, `zdp_services.c` | `subsys/zigbee/zdo/*.c` | adapted vendor source |
| `ss_apsEnDecrypt.c`, `ss_apsSecurityME.c`, `ss_nwkEnDecrypt.c`, `ss_tlCCM.c`, `ss_zdoSecurityME.c` | `subsys/zigbee/ss/*.c` | adapted vendor source |
| `zb_api.c` | `subsys/zigbee/zbapi/zb_api.c` | restored vendor API, Zephyr hook adaptation |
| `zb_af_data.c` | `subsys/zigbee/zdo/zdo_zephyr_glue.c` | Zephyr TX ownership/radio replacement |
| `zb_initialize.c` | `subsys/zigbee/common/zb_initialize.c` | adapted vendor source |
| `bdb_base.c` | `subsys/zigbee/bdb/bdb_base.c` + `bdb/bdb.c` + `platform/zephyr/zb_bdb_bootstrap.c` | split vendor BDB/API bootstrap |
| `second_clock.c` | `subsys/zigbee/platform/zephyr/zb_second_clock.c` | Zephyr timer replacement |
| `zb_buffer.c` | `subsys/zigbee/platform/zephyr/zb_buffer_zephyr.c` | Zephyr slab replacement |
| `zb_task_queue.c` | `subsys/zigbee/platform/zephyr/zb_task_queue_zephyr.c` + `zb_task_queue_router.c` | Zephyr queue replacement |
| `cGP_stub.c`, `dGP_stub.c`, `gp_base.c`, `gp_sec.c` | `subsys/zigbee/gp/*.c` (role-gated) | adapted vendor source |
| `nwk_test.c` | no runtime source required | vendor empty object |

The three platform replacements preserve the public contracts of their
vendor counterparts but do not copy vendor allocator/event internals into a
Zephyr build.  `zbapi/zb_api.c` is the functional API boundary: all ZDO
request wrappers serialize through the ported `zdo_send_req()` path, and ED
bootstrap operations delegate to the real NWK/ZDO manager rather than the
former inert phase-2 stubs.

The MAC transmit queue is defined by the ported `common/zb_config.c`, next to
the vendor table definitions.  No separate `*_compat.c` or ED stub source is
part of the build; the only remaining compatibility names are include-level
aggregators and ABI adapters required to compile the vendor translation units
against Zephyr.

## Parity checks

The source-set audit is intentionally mechanical:

```sh
python3 scripts/zigbee/libzigbee_port_parity.py
python3 scripts/zigbee/libzigbee_port_parity.py \
  --binary /tmp/zb-nsim-router/zephyr/zephyr.exe \
  --required-symbol zb_routerStart \
  --required-symbol zdo_nwkRouterStart
```

Exact object-code identity is not required; missing sources, missing symbols,
role-inappropriate weak fallbacks, and unowned buffers are port failures.
