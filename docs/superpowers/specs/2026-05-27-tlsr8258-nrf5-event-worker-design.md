# TLSR8258 event/worker driver design for Zigbee join and interview

## Context

The current TLSR8258 Zigbee port has already crossed the first RF launch boundary: after the vendor-aligned TX mode fix, the device emits `BeaconReq` on air and the coordinator responds with `Beacon`. Zigbee2MQTT now creates the device entry for IEEE `0xa4c138e050020002`, but interview still fails.

The remaining seam is between `TX done` and reliable admission of the next coordinator frame into the local receive path. Today that seam is spread across synchronous polling in `tlsr8258_tx()`, ad-hoc post-poll waiting in `tlsr8258_wait_for_post_poll_rx()`, RF IRQ handling, and the RX queue worker. This makes it hard to prove whether a failure is caused by missing RF RX capture, late DMA buffer reads, or ambiguous handoff from the driver to the Zigbee RX path.

`ieee802154_nrf5.c` provides a useful architectural reference. The relevant lesson is not Nordic-specific register programming, but the separation of concerns:

- ISR and radio callbacks capture events and transfer ownership only.
- Worker context owns frame handoff to upper layers.
- Synchronous driver APIs are implemented as waits on explicit asynchronous completion state.

## Goal

Restructure the TLSR8258 IEEE 802.15.4 driver so that join-critical radio progress is managed by an explicit event/worker state machine inside the driver, while preserving the existing direct-register TLSR RF backend.

This design targets the clean-flash path to successful Zigbee join and interview. It is intended to make the `BeaconReq -> Beacon -> AssocReq -> first coordinator response` path deterministic enough to complete interview on hardware.

## Non-goals

- Do not pull broad vendor bootstrap such as `drv_platform_init()` into Zephyr.
- Do not redesign the Zigbee stack above the driver unless later evidence proves the remaining seam is outside the driver.
- Do not turn this milestone into reboot restore or secure rejoin work.
- Do not replace the existing TLSR register backend with a vendor HAL clone.

## Recommended approach

Adopt the nRF5 event/worker architecture pattern inside `drivers/ieee802154/ieee802154_tlsr8258.c`:

1. Keep low-level TLSR RF programming in the existing helper layer.
2. Introduce explicit driver operation state for TX, post-TX RX wait, and failure completion.
3. Reduce the ISR to RF event capture and queue/completion signaling.
4. Let worker context own RX frame dispatch and join-critical post-TX response resolution.
5. Preserve the synchronous Zephyr radio API by waiting on driver completions instead of open-coded register polling loops.

## Driver architecture

### 1. RF backend remains direct-register

The following responsibilities stay in the current low-level helper layer:

- channel and power programming
- RX/TX mode register images
- DMA buffer setup
- RF start/stop
- raw IRQ register acknowledge and status decode

This design intentionally avoids importing nRF5-specific radio APIs or broad SDK bootstrap code.

### 2. Explicit operation state

The driver gains a small explicit state machine representing the radio operation currently in flight. At minimum it should cover:

- `IDLE`
- `TX_PENDING`
- `WAITING_POST_TX_RX`
- `COMPLETE_OK`
- `COMPLETE_NO_RX`
- `COMPLETE_RX_REJECTED`
- `COMPLETE_ERROR`

The operation record should carry only the data needed to complete the public API call reliably:

- operation kind
- expected completion class
- result code
- timeout budget
- whether a post-TX RX frame is required for success
- the sequence or lightweight frame discriminator needed for diagnostics

This state belongs to the driver, not to the Zigbee upper layer.

### 3. ISR responsibilities

The ISR should become strictly event-oriented:

- read and acknowledge RF cause
- on RX-ready, snapshot the DMA buffer and enqueue a stable RX frame object
- on TX-done, timeout, or error, update operation state and notify the waiter or worker
- avoid making final join-progress decisions in IRQ context

The ISR must not depend on `tlsr8258_tx()` keeping IRQs disabled while it polls for progress. Once RF is kicked, progress should be observable through state and completion primitives.

### 4. Worker responsibilities

`tlsr8258_rx_worker()` remains the sole owner of normal RX dispatch. Under this design it also becomes the place where join-critical post-TX progress is resolved:

- dequeue a stable RX snapshot
- run frame validation and filtering from stable memory
- pass valid frames to the Zigbee RX sink or net stack
- if the current driver operation is `WAITING_POST_TX_RX`, decide whether this RX frame completes that operation

This makes the key transition explicit: a frame is either never captured, captured but rejected by worker-side admission logic, or captured and forwarded successfully.

### 5. Synchronous public API over async internals

The Zephyr radio API remains synchronous from the caller's perspective. Internally:

1. `tlsr8258_tx()` prepares the TX buffer and operation state.
2. The driver arms the required IRQs and starts RF transmission.
3. `tlsr8258_tx()` waits on a completion primitive.
4. ISR and worker drive the operation to a final state.
5. `tlsr8258_tx()` returns the final mapped result.

This matches the useful part of the nRF5 model: public API stays synchronous, but actual radio progress is represented asynchronously and explicitly.

## Data flow

### TX path

The current long polling path in `tlsr8258_tx()` should be replaced with:

1. validate mode and optional CCA
2. build `tx_buffer`
3. initialize driver operation state
4. clear stale completion state
5. switch radio to TX and start DMA
6. wait for completion
7. map final operation result to the Zephyr API return code

`tlsr8258_wait_for_post_poll_rx()` should either disappear or become a thin wrapper over the new completion-driven flow. The current split between `tx()` and the helper is too implicit for this seam.

### RX path

The existing RX queue is already close to the desired model and should be retained. The key change is that join-critical decisions must be made from queued snapshots, not from mutable DMA state observed indirectly by multiple contexts.

## Error handling and timing

Join-critical outcomes should become distinguishable:

- TX failed before airtime
- TX completed but no relevant RX arrived before timeout
- RX arrived but was rejected by admission logic
- RX arrived and completed the pending operation

Timeouts must be explicit per operation and owned by the driver state machine. Returning to RX mode after timeout or error must be part of the completion path, not an ad-hoc cleanup branch.

## Files in scope

Primary implementation target:

- `drivers/ieee802154/ieee802154_tlsr8258.c`

Likely focused tests to update:

- `tests/unit/tlsr8258_rf_irq/main.c`
- `tests/unit/tlsr8258_tx_irq/main.c`
- `tests/unit/tlsr8258_beacon_request_wait_path/main.c`

Possible follow-up consumers, but not first-line refactor targets:

- `subsys/zigbee/platform/zephyr/drv_radio_zephyr.c`
- join/interview-focused Zigbee tests only if driver evidence proves the seam moved upward

## Validation strategy

### Focused regressions

Add or update narrow tests to prove:

1. `tlsr8258_tx()` waits on completion state rather than local register polling.
2. TX success plus queued RX can complete a `WAITING_POST_TX_RX` operation.
3. Timeout and RF error paths restore RX mode and produce deterministic return codes.
4. Existing RX queue and worker behavior remains intact for non-join traffic.

### Hardware acceptance

Use the existing clean-flash hardware procedure from `AGENTS.md`:

1. flash current image
2. blank `nvs_storage` to `0xFF`
3. hard reset
4. open permit-join
5. capture channel 11 traffic

Success for this design milestone means the board deterministically reaches:

- `BeaconReq` on air
- coordinator `Beacon`
- `AssocReq`
- first coordinator response admitted locally through the driver
- progression into interview without immediate drop-out caused by the driver seam

## Trade-offs

### Benefits

- Makes the current failure boundary observable and testable.
- Reuses the strongest architectural idea from `ieee802154_nrf5.c` without copying unrelated code.
- Keeps TLSR RF register logic local and evidence-driven.
- Avoids moving debugging uncertainty into the Zigbee upper layers too early.

### Costs

- More invasive than a single register fix.
- Requires carefully preserving existing ACK and RX queue behavior.
- May surface a higher-layer bug once the driver seam is closed, but that is still progress because the boundary becomes explicit.

## Acceptance criteria

This design is successful when:

- the TLSR driver no longer relies on the current open-coded post-TX polling seam for join-critical progress
- driver outcomes for TX success, timeout, RX rejection, and RX acceptance are explicit and testable
- clean-flash hardware progress moves beyond the current `BeaconReq visible, interview failed` boundary
- any remaining failure after this refactor can be shown to sit above the driver with concrete evidence
