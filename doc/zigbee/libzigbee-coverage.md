.. SPDX-License-Identifier: Apache-2.0

# libzigbee coverage

`subsys/zigbee` vendors two upstreams verbatim and adapts them around the
edges. This table says what happens to every imported file, so a piece of the
stack cannot go missing without someone writing it down.

Refresh the imports with `scripts/zigbee/import_vendor.sh`; see that script for
the order to amend and rebase in.

Every imported file is in exactly one category:

- **as-is** — compiled straight from the import, unmodified.
- **patched** — compiled with a recorded Zephyr-side change.
- **replaced** — not compiled; the platform layer provides the same API.
- **unused** — not compiled, and nothing needs it.

## libzigbee — the reconstructed stack

47 of 51 sources are compiled **as-is**: the MAC, NWK, APS, security service,
ZDO, second clock and `zbapi` translation units, plus the Green Power stubs the
MAC links against on router and coordinator builds.

| file | category | why |
|---|---|---|
| `common/zb_buffer.c` | replaced | `platform/zephyr/zb_buffer_zephyr.c` backs the same buffer API on a Zephyr pool. The vendor pool object (`g_mPool`, `zb_buf_pool_t`) is not mirrored; references resolve through `zb_buf_from_ref()`/`zb_buf_to_ref()`. |
| `common/zb_task_queue.c` | replaced | `platform/zephyr/zb_task_queue_zephyr.c` runs the queues on kernel primitives, with a separate RX lane so radio delivery cannot be starved by control callbacks. |
| `af/zb_af_data.c` | replaced | The port's APS transmit path lives in `platform/zephyr/zb_zbhci_cmd.c` and `zdo/zdo_zephyr_glue.c`. |
| `nwk/nwk_test.c` | unused | Empty vendor test stub. |

Symbol-level exceptions inside otherwise as-is files:

| symbol | what happens | why |
|---|---|---|
| `zb_info_save` | renamed to `zb_info_save_vendor` at compile time (`set_source_files_properties` on `zbapi/zb_initialize.c`) | The Zephyr persistence layer writes a versioned blob covering the network context and the frame counter, where the imported version stores only `g_zbInfo`. |
| `zb_init` | not called | The port runs its own bootstrap in `platform/zephyr/zb_main.c`, which calls the individual init entry points and starts the second clock itself. |
| `rf802154_tx_ready` | Zephyr port expands the imported beacon header before radio handoff | The imported builder uses compressed PAN addressing (`0x8043`), but a standards-compliant beacon needs the source PAN field; changing only the FCF shifts the source address and beacon payload by two bytes. |
| `mac_rxDataParse` | Keeps active-scan beacons on the queued indication path | The vendor RF path consumes these beacons before the queued parser; Zephyr has no equivalent earlier callback, so dropping them prevents NWK discovery. |
| `tl_zbMlmeCmdBeaconReqRecvd` | Releases the received beacon-request buffer after scheduling the response | The response uses the dedicated MAC TX buffer; retaining the RX indication leaks one Zephyr buffer per request until the coordinator crashes. |
| `nwk_associateJoin` | Keeps the coordinator tuple of the MAC PIB in sync with the chosen parent, fills a missing role capability, and enters the joining state before posting the request | The native-socket medium delivers the indirect association response before the confirm that would fill the tuple, the bootstrap can post a join without the mandatory address-allocation capability, and the imported stack only enters the joining state once the confirm arrives. |
| `tl_zbPhyMlmeIndicate` | Dispatches data requests while waiting for an indirect association response | The imported wait-state gate discarded the poll that must release the coordinator's queued association response. |
| `tl_zbMlmeCmdDataReqRecvd` | Converts the received MAC header into the compact data-request record consumed by indirect delivery | The Zephyr RX path passed the metadata buffer directly, so the callback read pointer bytes instead of the polling device address and never released the queued association response. |
| `tl_zbMlmeCmdAssociateRespRecvd` | Accepts a response while the local association request is outstanding | The native-sim medium delivers the indirect response as a normal RX frame, so the imported state-only gate discarded a valid response before the MAC confirm. |
| `zb_radio_mark_pending_data` | Native-socket RX marks matching indirect transactions ready before MAC parsing | The vendor RF IRQ performs this transition before the imported MAC callback; the native socket RX path bypasses that IRQ. |
| `g_routingTab`, `ROUTING_TABLE_SIZE`, `NWKC_RREQ_RETRIES`, `NWKC_INITIAL_RREQ_RETRIES`, `NWKC_TRANSFAILURE_CNT_THRESHOLD`, `g_routeRecTab`, `NWK_ROUTE_RECORD_TABLE_SIZE` | taken from the imported stack; `common/zb_config.c` no longer defines them | The vendor archive does not export these, so they belong to the application. The imported stack defining them diverges from the vendor; until that is settled upstream, the application side yields. |

## tl_zigbee_sdk — the open SDK

179 files, imported with CRLF converted to LF and otherwise byte for byte the
vendor ones. `aps/aps_stackUse.h` comes from a newer vendor release than the
rest of the checkout, so the refresh script skips it with a warning rather than
overwriting it.

Three vendor headers are **replaced** rather than imported, under the same
names in `common/includes/`, so imported sources include them unchanged:

| header | why |
|---|---|
| `zb_common.h` | The vendor header pulls `tl_common.h` and with it the chip_8258 platform. The replacement carries the same surface: buffer and task queue API, diagnostics counters, default keys, network tables, and the per-layer section attributes as no-ops. |
| `zb_buffer.h` | Declares the buffer API against the Zephyr pool; drops the vendor pool layout. |
| `zb_task_queue.h` | The types live in `zb_common.h` here. |

The remaining SDK adaptations are in
`zigbee: adapt the imported SDK sources to Zephyr`.

## Known gaps

Port logic that lived inside libzigbee files on the previous branch and did not
survive the verbatim import. None of it is in the stack today:

- the router join latch is defined in `zdo/zdo_zephyr_glue.c` but nothing sets
  or reads it, because `zdo_live_join_context()` and its call sites are gone;
- the NWK command handlers the port wrote in `nwk_data.c`, the join-path helpers
  in `nwk_join.c`, the MAC receive helpers in `mac_trx.c`, and the previous-child
  check in `zdp_services.c`.

These are Zephyr-side work: libzigbee stays a faithful reconstruction of the
vendor archive, so port-invented logic does not go upstream.
