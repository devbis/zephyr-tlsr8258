# TLSR8258 RX Rearchitecture Design

## Goal

Replace the current split TLSR8258 receive path with a driver-owned RX pipeline modeled on the architectural strengths of `drivers/ieee802154/ieee802154_nrf5.c`, so that:

- RF IRQ handling is thin and deterministic.
- RX buffer ownership is single-source and explicit.
- Zigbee and regular Zephyr IEEE 802.15.4 consumers both receive frames through one authoritative software handoff.
- The existing hardware symptom is removed: coordinator beacons already present in DMA must reliably reach the Zigbee software path.

## Problem Statement

The current TLSR8258 RX path is over-split:

1. RF DMA writes into `tlsr8258_radio.rx_buffer`.
2. The native ISR snapshots into `rx_shadow`.
3. The driver callback enters `drv_radio_zephyr.c`.
4. The bridge copies again into its own slot queue.
5. A `k_work` handler later calls `zb_macDataRecvHandler()` or falls back to `rf_rx_irq_handler()`.

This creates multiple ownership layers and multiple potential drop points. Hardware evidence already shows that the radio/DMA layer is receiving valid coordinator beacons, but those frames do not consistently advance into bridge counters, NWK counters, or interview progress. The failure is therefore in the software handoff after DMA reception.

## Reference Pattern

`ieee802154_nrf5.c` uses a simpler receive architecture:

- the IRQ handler is a thin wrapper;
- received frames enter one driver-owned queue;
- one RX thread consumes that queue;
- the thread delivers frames upward in process context;
- buffer ownership stays with the driver until delivery is complete.

The TLSR8258 design should copy this pattern at the architecture level, not at the register-programming level.

## Chosen Approach

Adopt a **driver-owned RX queue/worker** design.

- `ieee802154_tlsr8258.c` becomes the single owner of post-DMA RX frames.
- `drv_radio_zephyr.c` stops owning its own RX queue and becomes a thin Zigbee sink/adapter.
- The regular Zephyr IEEE 802.15.4 / RAW path remains supported through the same driver-owned RX pipeline.

## Architecture

### 1. Thin ISR

`tlsr8258_rf_isr()` remains responsible only for:

- reading and acknowledging RF IRQ state;
- identifying RX-complete events;
- invoking a minimal capture helper;
- waking the RX worker when a frame has been queued.

It must not directly call Zigbee bridge logic, `k_work_submit()`, `zb_macDataRecvHandler()`, or `net_recv_data()`.

### 2. Driver-Owned RX Frame Queue

The TLSR8258 driver will own:

- a fixed-size array of preallocated RX frame slots;
- queue metadata for producer/consumer indexing;
- frame metadata per slot, including length, RSSI, and flags needed by upper layers.

DMA ingress remains `tlsr8258_radio.rx_buffer`. After an RX event, the ISR capture helper copies the frame once into a free driver-owned slot. That slot becomes the authoritative software copy of the frame.

`rx_shadow` and the bridge-owned slot queue are removed as independent ownership layers.

### 3. RX Worker

A dedicated RX worker in the driver consumes queued slots in thread context.

The worker is the only code that dispatches received frames upward. This makes RX handoff deterministic and removes the need for a second deferred-processing queue in the Zigbee bridge.

### 4. Unified Sink Contract

The driver exposes one RX sink contract for upper layers:

- **Zigbee sink**: used when the Zigbee stack registers for RX delivery.
- **Zephyr sink**: used for the regular IEEE 802.15.4 / RAW networking path.

At delivery time, each frame goes through exactly one sink path. The system must not maintain two competing RX pipelines for the same received frame.

## Detailed Data Flow

### RX Event Path

1. RF hardware raises RX-complete IRQ.
2. `tlsr8258_rf_isr()` acknowledges the IRQ and calls `tlsr8258_rx_capture_isr()`.
3. `tlsr8258_rx_capture_isr()`:
   - validates the minimal DMA frame shape needed for safe capture;
   - allocates a free driver RX slot;
   - copies DMA bytes and metadata into that slot;
   - records drop statistics if no slot is available;
   - signals the RX worker.
4. `tlsr8258_rx_worker()` dequeues the slot.
5. The worker dispatches to the active sink:
   - Zigbee sink in `drv_radio_zephyr.c`, or
   - Zephyr `net_pkt` path.
6. After sink processing completes, the driver releases the slot back to the free pool.

### Zigbee Delivery

For Zigbee builds, `zb_radio_port_register_rx_cb()` will register a sink that consumes a stable frame snapshot in thread context. `drv_radio_zephyr.c` will no longer manage RX slot allocation, queueing, or `k_work` based delivery for normal RX.

It remains responsible for Zigbee-side adaptation: extracting the PSDU form expected by the Zigbee MAC and calling the correct Zigbee receive entrypoint.

### Zephyr / RAW Delivery

For regular IEEE 802.15.4 operation, the worker builds the `net_pkt` and calls `net_recv_data()` from thread context, preserving the existing device API and keeping RAW mode support on the same authoritative RX path.

## File-Level Changes

### `drivers/ieee802154/ieee802154_tlsr8258.c`

- Add driver-owned RX slot storage and queue bookkeeping.
- Add ISR capture helper.
- Add dedicated RX worker/thread.
- Route received frames through one sink dispatch point.
- Preserve existing public radio API entrypoints (`start`, `stop`, `filter`, `set_channel`, `tx`, and related configuration methods).

### `subsys/zigbee/platform/zephyr/zb_radio_port_tlsr8258.c`

- Keep registration of the Zigbee RX sink.
- Update the callback contract to receive stable frame snapshots in thread context instead of borrowed ISR-time DMA state.

### `subsys/zigbee/platform/zephyr/drv_radio_zephyr.c`

- Remove bridge-owned RX slot queue and `k_work` scheduling for normal receive flow.
- Keep Zigbee-specific adaptation and receive delivery only.
- Retain counters and diagnostics that are still useful for validating receipt into the Zigbee stack.

## Compatibility Rules

- The external `ieee802154_radio_api` must remain unchanged.
- Zigbee and ordinary Zephyr IEEE 802.15.4 / RAW operation must both keep working.
- TX behavior is not comprehensively redesigned in this change; TX is only adjusted where needed to stay compatible with the new RX ownership model.
- The fix targets the receive handoff architecture, not unrelated commissioning or persistence logic.

## Error Handling

- RX queue overflow increments explicit drop counters.
- Invalid DMA-shaped frames are rejected at capture time without creating partially owned software state.
- Sink failures do not leak driver RX slots.
- The driver, not the bridge, is responsible for lifecycle cleanup of queued RX frames.

## Validation Plan

### Automated

- Add unit coverage for:
  - ISR enqueue into the driver RX queue;
  - worker dequeue and single-sink dispatch;
  - queue overflow handling;
  - stable delivery ordering;
  - preservation of Zephyr path behavior where applicable.
- Update existing TLSR8258 RF IRQ tests to reflect the thin-ISR model rather than bridge-owned deferred RX delivery.
- Re-run existing Zigbee host baselines that cover bootstrap, interview behavior, join recovery, and persistence-sensitive flows.

### Hardware

Validate the following on the TLSR8258 board:

1. coordinator beacons seen in DMA now increment bridge/MAC-side receive counters;
2. interview progresses instead of stopping at `ZDO_NO_MATCH`;
3. the device remains joined long enough for post-interview traffic;
4. reboot restore still works;
5. leave/reset still clears state cleanly for rejoin.

## Non-Goals

- No attempt to match nRF5 register or vendor-library behavior at the hardware backend level.
- No broad rewrite of the TX state machine.
- No unrelated refactoring in persistence, BDB, or application profile code beyond what is required by the RX contract change.

## Risks and Mitigations

- **Risk:** queue sizing may be too small for bursty traffic.  
  **Mitigation:** start with a fixed bounded queue and expose counters for overflow analysis.

- **Risk:** Zigbee bridge assumptions may still depend on old DMA-style buffer layout.  
  **Mitigation:** keep the sink contract explicit about buffer shape and update bridge extraction in one place.

- **Risk:** RAW / Zephyr net behavior regresses while fixing Zigbee.  
  **Mitigation:** keep the driver sink abstraction generic and validate both sink paths.

## Success Criteria

This redesign is successful when all of the following are true:

- there is one authoritative post-DMA RX queue in the TLSR8258 driver;
- `drv_radio_zephyr.c` no longer owns the normal RX queue/work pipeline;
- received coordinator beacons reach the Zigbee software path on hardware;
- interview completes and post-join traffic continues;
- existing external radio APIs remain intact.
