# TLSR8258 RX Rearchitecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the split TLSR8258 RX handoff with a driver-owned RX queue/worker so Zigbee and normal Zephyr IEEE 802.15.4 traffic both travel through one authoritative post-DMA software path.

**Architecture:** Keep the external `ieee802154_radio_api` and Zigbee radio-port surface intact, but move RX ownership into the TLSR8258 driver. Mirror the `ieee802154_nrf5.c` pattern: thin RF ISR, driver-owned RX slots, one RX worker, and one sink dispatch point that selects either Zigbee adaptation or the regular Zephyr networking path.

**Tech Stack:** Zephyr IEEE 802.15.4 driver API, TLSR8258 radio driver, Zigbee platform adapter, standalone C unit tests via CMake/CTest, Zigbee host shell tests, `west`, `rtk`, `TlsrPgm_local.py`, sniffer capture.

---

## File structure

- `drivers/ieee802154/ieee802154_tlsr8258_rx_queue.h`
  - New queue contract for driver-owned RX slots and metadata.
- `drivers/ieee802154/ieee802154_tlsr8258_rx_queue.c`
  - New queue implementation used by the TLSR8258 driver ISR/worker pair.
- `drivers/ieee802154/ieee802154_tlsr8258.c`
  - Thin ISR, RX capture helper, RX worker, unified sink dispatch.
- `drivers/ieee802154/CMakeLists.txt`
  - Link the new RX queue helper into the TLSR8258 driver build.
- `include/zephyr/drivers/ieee802154/tlsr8258_zigbee_bridge.h`
  - Replace the ISR-time callback contract with a stable thread-context RX sink contract.
- `subsys/zigbee/include/zephyr/zigbee/zb_radio_port.h`
  - Mirror the new sink registration API into the generic Zigbee radio-port surface.
- `subsys/zigbee/platform/zephyr/zb_radio_port_tlsr8258.c`
  - Register the Zigbee RX sink against the new driver contract.
- `subsys/zigbee/platform/zephyr/drv_radio_zephyr.c`
  - Remove bridge-owned RX queue/work ownership and keep only Zigbee-side adaptation and counters.
- `tests/unit/tlsr8258_rx_queue/CMakeLists.txt`
  - New standalone test target for queue semantics.
- `tests/unit/tlsr8258_rx_queue/main.c`
  - New queue regression tests for enqueue/dequeue/overflow behavior.
- `tests/unit/tlsr8258_rf_irq/main.c`
  - Lock the thin-ISR RX-event semantics while the worker model moves into the driver.
- `tests/subsys/zigbee/host_sim/run.sh`
  - Add a regression guard that `drv_radio_zephyr.c` no longer owns an RX queue/work pipeline.
- `tests/subsys/zigbee/host_shell_bootstrap/run.sh`
  - Existing host bootstrap regression to rerun after the bridge contract change.
- `tests/subsys/zigbee/host_app_profile/run.sh`
  - Existing profile regression to rerun after the bridge contract change.

### Task 1: Introduce a driver-owned RX queue helper

**Files:**
- Create: `drivers/ieee802154/ieee802154_tlsr8258_rx_queue.h`
- Create: `drivers/ieee802154/ieee802154_tlsr8258_rx_queue.c`
- Modify: `drivers/ieee802154/CMakeLists.txt`
- Create: `tests/unit/tlsr8258_rx_queue/CMakeLists.txt`
- Create: `tests/unit/tlsr8258_rx_queue/main.c`

- [ ] **Step 1: Write the failing queue regression**

Create `tests/unit/tlsr8258_rx_queue/main.c` with focused queue tests:

```c
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../../drivers/ieee802154/ieee802154_tlsr8258_rx_queue.h"

static int failures;

static void expect_true(bool expr, const char *msg)
{
	if (!expr) {
		fprintf(stderr, "FAIL: %s\n", msg);
		failures++;
	}
}

static void test_enqueue_dequeue_round_trip(void)
{
	struct tlsr8258_rx_queue queue;
	struct tlsr8258_rx_slot slots[2];
	uint8_t dma[] = { 11u, 0u, 0u, 0u, 8u, 0xaa, 0xbb, 0xcc };
	struct tlsr8258_rx_frame frame;

	tlsr8258_rx_queue_init(&queue, slots, 2u);
	expect_true(tlsr8258_rx_queue_try_enqueue(&queue, dma, sizeof(dma), -42), "enqueue");
	expect_true(tlsr8258_rx_queue_try_dequeue(&queue, &frame), "dequeue");
	expect_true(frame.len == sizeof(dma), "len preserved");
	expect_true(frame.rssi_dbm == -42, "rssi preserved");
	expect_true(memcmp(frame.dma, dma, sizeof(dma)) == 0, "dma preserved");
}

static void test_queue_overflow_is_reported(void)
{
	struct tlsr8258_rx_queue queue;
	struct tlsr8258_rx_slot slots[1];
	uint8_t dma[] = { 9u, 0u, 0u, 0u, 6u, 0x61, 0x88 };

	tlsr8258_rx_queue_init(&queue, slots, 1u);
	expect_true(tlsr8258_rx_queue_try_enqueue(&queue, dma, sizeof(dma), -55), "first enqueue");
	expect_true(!tlsr8258_rx_queue_try_enqueue(&queue, dma, sizeof(dma), -55), "overflow reported");
	expect_true(tlsr8258_rx_queue_drop_count(&queue) == 1u, "drop count increments");
}

int main(void)
{
	test_enqueue_dequeue_round_trip();
	test_queue_overflow_is_reported();
	return failures == 0 ? 0 : 1;
}
```

Create `tests/unit/tlsr8258_rx_queue/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(tlsr8258_rx_queue C)
include(CTest)

add_executable(tlsr8258_rx_queue
    main.c
    ../../../drivers/ieee802154/ieee802154_tlsr8258_rx_queue.c
)

add_test(NAME tlsr8258_rx_queue COMMAND tlsr8258_rx_queue)
```

- [ ] **Step 2: Run the queue test and verify it fails**

Run:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
cmake -S tests/unit/tlsr8258_rx_queue -B /tmp/tlsr8258-rx-queue
cmake --build /tmp/tlsr8258-rx-queue
```

Expected: FAIL with `ieee802154_tlsr8258_rx_queue.h` / `ieee802154_tlsr8258_rx_queue.c` missing.

- [ ] **Step 3: Write the minimal queue helper**

Create `drivers/ieee802154/ieee802154_tlsr8258_rx_queue.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TLSR8258_RX_SLOT_DMA_SIZE 256u

struct tlsr8258_rx_frame {
	uint8_t *dma;
	uint8_t len;
	int8_t rssi_dbm;
};

struct tlsr8258_rx_slot {
	uint8_t dma[TLSR8258_RX_SLOT_DMA_SIZE];
	uint8_t len;
	int8_t rssi_dbm;
	bool queued;
};

struct tlsr8258_rx_queue {
	struct tlsr8258_rx_slot *slots;
	uint8_t slot_count;
	uint8_t head;
	uint8_t tail;
	uint8_t pending;
	uint32_t drop_count;
};

void tlsr8258_rx_queue_init(struct tlsr8258_rx_queue *queue,
			    struct tlsr8258_rx_slot *slots,
			    uint8_t slot_count);
bool tlsr8258_rx_queue_try_enqueue(struct tlsr8258_rx_queue *queue,
				   const uint8_t *dma,
				   uint8_t dma_len,
				   int8_t rssi_dbm);
bool tlsr8258_rx_queue_try_dequeue(struct tlsr8258_rx_queue *queue,
				   struct tlsr8258_rx_frame *frame);
uint32_t tlsr8258_rx_queue_drop_count(const struct tlsr8258_rx_queue *queue);
```

Create `drivers/ieee802154/ieee802154_tlsr8258_rx_queue.c`:

```c
#include "ieee802154_tlsr8258_rx_queue.h"

#include <string.h>

void tlsr8258_rx_queue_init(struct tlsr8258_rx_queue *queue,
			    struct tlsr8258_rx_slot *slots,
			    uint8_t slot_count)
{
	memset(queue, 0, sizeof(*queue));
	memset(slots, 0, sizeof(*slots) * slot_count);
	queue->slots = slots;
	queue->slot_count = slot_count;
}

bool tlsr8258_rx_queue_try_enqueue(struct tlsr8258_rx_queue *queue,
				   const uint8_t *dma,
				   uint8_t dma_len,
				   int8_t rssi_dbm)
{
	struct tlsr8258_rx_slot *slot;

	if (queue->pending >= queue->slot_count) {
		queue->drop_count++;
		return false;
	}

	slot = &queue->slots[queue->tail];
	memcpy(slot->dma, dma, dma_len);
	slot->len = dma_len;
	slot->rssi_dbm = rssi_dbm;
	slot->queued = true;
	queue->tail = (uint8_t)((queue->tail + 1u) % queue->slot_count);
	queue->pending++;
	return true;
}

bool tlsr8258_rx_queue_try_dequeue(struct tlsr8258_rx_queue *queue,
				   struct tlsr8258_rx_frame *frame)
{
	struct tlsr8258_rx_slot *slot;

	if (queue->pending == 0u) {
		return false;
	}

	slot = &queue->slots[queue->head];
	frame->dma = slot->dma;
	frame->len = slot->len;
	frame->rssi_dbm = slot->rssi_dbm;
	slot->queued = false;
	queue->head = (uint8_t)((queue->head + 1u) % queue->slot_count);
	queue->pending--;
	return true;
}

uint32_t tlsr8258_rx_queue_drop_count(const struct tlsr8258_rx_queue *queue)
{
	return queue->drop_count;
}
```

Update `drivers/ieee802154/CMakeLists.txt`:

```cmake
zephyr_library_sources_ifdef(CONFIG_IEEE802154_TELINK_TLSR8258
  ieee802154_tlsr8258.c
  ieee802154_tlsr8258_poll_wait.c
  ieee802154_tlsr8258_rf_irq.c
  ieee802154_tlsr8258_rx_queue.c
  ieee802154_tlsr8258_tx_irq.c
)
```

- [ ] **Step 4: Run the queue test and verify it passes**

Run:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
cmake -S tests/unit/tlsr8258_rx_queue -B /tmp/tlsr8258-rx-queue
cmake --build /tmp/tlsr8258-rx-queue
ctest --test-dir /tmp/tlsr8258-rx-queue --output-on-failure
```

Expected: `100% tests passed`.

- [ ] **Step 5: Commit the queue primitive**

Run:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
git add \
  drivers/ieee802154/CMakeLists.txt \
  drivers/ieee802154/ieee802154_tlsr8258_rx_queue.h \
  drivers/ieee802154/ieee802154_tlsr8258_rx_queue.c \
  tests/unit/tlsr8258_rx_queue/CMakeLists.txt \
  tests/unit/tlsr8258_rx_queue/main.c
git commit -m "ieee802154: add tlsr8258 rx queue helper"
```

### Task 2: Move the TLSR8258 driver to a thin ISR plus RX worker

**Files:**
- Modify: `drivers/ieee802154/ieee802154_tlsr8258.c`
- Modify: `include/zephyr/drivers/ieee802154/tlsr8258_zigbee_bridge.h`
- Modify: `subsys/zigbee/include/zephyr/zigbee/zb_radio_port.h`
- Modify: `tests/unit/tlsr8258_rf_irq/main.c`

- [ ] **Step 1: Extend the RF IRQ regression for the thin-ISR contract**

Add this test to `tests/unit/tlsr8258_rf_irq/main.c`:

```c
static void test_non_rx_irq_bits_do_not_trigger_rx_capture(void)
{
	EXPECT_FALSE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_TX));
	EXPECT_FALSE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_TX | RF_IRQ_RX_DR));
	EXPECT_FALSE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX_CRC_2));
}
```

Call it from `main()` before the final PASS print.

- [ ] **Step 2: Run the RF IRQ test and verify it fails only on the new expectation**

Run:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
cmake -S tests/unit/tlsr8258_rf_irq -B /tmp/tlsr8258-rf-irq
cmake --build /tmp/tlsr8258-rf-irq
ctest --test-dir /tmp/tlsr8258-rf-irq --output-on-failure
```

Expected: FAIL in `test_non_rx_irq_bits_do_not_trigger_rx_capture`.

- [ ] **Step 3: Replace the ISR-time callback contract and add the RX worker**

Update `include/zephyr/drivers/ieee802154/tlsr8258_zigbee_bridge.h`:

```c
struct tlsr8258_rx_frame_view {
	const uint8_t *dma;
	uint8_t len;
	int8_t rssi_dbm;
};

typedef int (*tlsr8258_zigbee_rx_sink_t)(const struct tlsr8258_rx_frame_view *frame);

void tlsr8258_zigbee_register_rx_sink(tlsr8258_zigbee_rx_sink_t sink);
```

Update `subsys/zigbee/include/zephyr/zigbee/zb_radio_port.h`:

```c
struct tlsr8258_rx_frame_view;
typedef int (*zb_radio_port_rx_sink_t)(const struct tlsr8258_rx_frame_view *frame);

void zb_radio_port_register_rx_sink(zb_radio_port_rx_sink_t sink);
```

In `drivers/ieee802154/ieee802154_tlsr8258.c`, add the worker-owned RX state:

```c
#define TLSR8258_RX_WORKER_STACK_SIZE 768
#define TLSR8258_RX_SLOT_COUNT 4u

K_KERNEL_STACK_MEMBER(tlsr8258_rx_worker_stack, TLSR8258_RX_WORKER_STACK_SIZE);
static struct k_sem tlsr8258_rx_sem;
static struct k_thread tlsr8258_rx_worker_thread;
static struct tlsr8258_rx_queue tlsr8258_rx_queue;
static struct tlsr8258_rx_slot tlsr8258_rx_slots[TLSR8258_RX_SLOT_COUNT];
static tlsr8258_zigbee_rx_sink_t tlsr8258_zigbee_rx_sink;
```

Register the new sink:

```c
void tlsr8258_zigbee_register_rx_sink(tlsr8258_zigbee_rx_sink_t sink)
{
	tlsr8258_zigbee_rx_sink = sink;
}
```

Add thread-context dispatch:

```c
static int tlsr8258_dispatch_rx_frame(const struct tlsr8258_rx_frame *frame)
{
	struct tlsr8258_rx_frame_view view = {
		.dma = frame->dma,
		.len = frame->len,
		.rssi_dbm = frame->rssi_dbm,
	};

	if (tlsr8258_zigbee_rx_sink != NULL) {
		return tlsr8258_zigbee_rx_sink(&view);
	}

	return tlsr8258_deliver_net_pkt(frame->dma, frame->len, frame->rssi_dbm);
}

static void tlsr8258_rx_worker(void *arg1, void *arg2, void *arg3)
{
	struct tlsr8258_rx_frame frame;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		k_sem_take(&tlsr8258_rx_sem, K_FOREVER);
		while (tlsr8258_rx_queue_try_dequeue(&tlsr8258_rx_queue, &frame)) {
			(void)tlsr8258_dispatch_rx_frame(&frame);
		}
	}
}
```

Make the ISR thin:

```c
static void tlsr8258_rx_capture_isr(void)
{
	uint8_t dma_len = (uint8_t)MIN((uint16_t)tlsr8258_radio.rx_buffer[0] + 4u,
				       (uint16_t)TLSR8258_RX_SLOT_DMA_SIZE);
	int8_t rssi_dbm = tlsr8258_dma_rssi_dbm(tlsr8258_radio.rx_buffer);

	TLSR_REG16(0x0f20) = RF_IRQ_RX;
	tlsr8258_radio_rx_count_inc();
	if (tlsr8258_rx_queue_try_enqueue(&tlsr8258_rx_queue,
					 tlsr8258_radio.rx_buffer,
					 dma_len,
					 rssi_dbm)) {
		k_sem_give(&tlsr8258_rx_sem);
	}
}

static void tlsr8258_rf_isr(const void *unused)
{
	uint16_t irq = TLSR_REG16(0x0f20);
	uint16_t effective_irq = tlsr8258_rf_irq_effective_status(
		irq, tlsr8258_radio.rx_buffer, sizeof(tlsr8258_radio.rx_buffer));

	ARG_UNUSED(unused);
	tlsr8258_radio_last_irq_set(effective_irq);

	if (tlsr8258_rf_irq_has_rx_event(effective_irq)) {
		tlsr8258_rx_capture_isr();
		return;
	}

	TLSR_REG16(0x0f20) = effective_irq != 0u ? effective_irq : RF_IRQ_ALL;
}
```

Initialize the queue and worker from `tlsr8258_init()`:

```c
tlsr8258_rx_queue_init(&tlsr8258_rx_queue, tlsr8258_rx_slots, TLSR8258_RX_SLOT_COUNT);
k_sem_init(&tlsr8258_rx_sem, 0, TLSR8258_RX_SLOT_COUNT);
k_thread_create(&tlsr8258_rx_worker_thread, tlsr8258_rx_worker_stack,
		K_KERNEL_STACK_SIZEOF(tlsr8258_rx_worker_stack),
		tlsr8258_rx_worker, NULL, NULL, NULL,
		K_PRIO_COOP(2), 0, K_NO_WAIT);
```

- [ ] **Step 4: Re-run the RF IRQ regression and a real build**

Run:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
cmake -S tests/unit/tlsr8258_rf_irq -B /tmp/tlsr8258-rf-irq
cmake --build /tmp/tlsr8258-rf-irq
ctest --test-dir /tmp/tlsr8258-rf-irq --output-on-failure
rtk ../.venv-zephyr/bin/west build -b tlsr8258_tb03f samples/zigbee/zigbee_shell \
  -d ../build-zephyr-zigbee-shell-rx-rework --pristine
```

Expected:

- `tlsr8258_rf_irq: PASS`
- `west build` completes successfully.

- [ ] **Step 5: Commit the driver RX worker conversion**

Run:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
git add \
  drivers/ieee802154/ieee802154_tlsr8258.c \
  include/zephyr/drivers/ieee802154/tlsr8258_zigbee_bridge.h \
  subsys/zigbee/include/zephyr/zigbee/zb_radio_port.h \
  tests/unit/tlsr8258_rf_irq/main.c
git commit -m "ieee802154: move tlsr8258 rx handoff into driver worker"
```

### Task 3: Turn `drv_radio_zephyr.c` into a thin Zigbee RX sink

**Files:**
- Modify: `subsys/zigbee/platform/zephyr/drv_radio_zephyr.c`
- Modify: `subsys/zigbee/platform/zephyr/zb_radio_port_tlsr8258.c`
- Modify: `subsys/zigbee/include/zephyr/zigbee/zb_radio_port.h`
- Modify: `tests/subsys/zigbee/host_sim/run.sh`
- Test: `tests/subsys/zigbee/host_shell_bootstrap/run.sh`
- Test: `tests/subsys/zigbee/host_app_profile/run.sh`

- [ ] **Step 1: Add a failing host regression that forbids bridge-owned RX queue/work**

Add this guard to `tests/subsys/zigbee/host_sim/run.sh`:

```sh
if rg -n 'struct zb_radio_rx_slot|zb_radio_rx_slot_alloc|zb_radio_rx_slot_pop|k_work_init\\(&g_radio_rx_work|zb_radio_rx_work_handler' \
	subsys/zigbee/platform/zephyr/drv_radio_zephyr.c; then
	echo "drv_radio_zephyr.c must be a thin Zigbee RX sink, not an RX queue owner" >&2
	exit 1
fi
```

- [ ] **Step 2: Run the host regression and verify it fails on the old bridge design**

Run:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
tests/subsys/zigbee/host_sim/run.sh
```

Expected: FAIL with `drv_radio_zephyr.c must be a thin Zigbee RX sink, not an RX queue owner`.

- [ ] **Step 3: Remove the bridge queue/work and keep only Zigbee adaptation**

In `subsys/zigbee/platform/zephyr/drv_radio_zephyr.c`, delete:

- `struct zb_radio_rx_slot`
- `g_radio_rx_work`
- `zb_radio_rx_slot_alloc()`
- `zb_radio_rx_slot_pop()`
- `zb_radio_rx_slot_release()`
- `zb_radio_on_rx()`
- `zb_radio_rx_work_handler()`

Replace them with a sink that consumes stable frames:

```c
static int zb_radio_consume_rx_frame(const struct tlsr8258_rx_frame_view *frame)
{
	const uint8_t *psdu = NULL;
	uint8_t psdu_len = 0U;
	u8 *saved_rx_buf;

	if ((frame == NULL) || (frame->dma == NULL) || (frame->len == 0U)) {
		atomic_inc(&g_radio.rx_drop_count);
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_INVALID_RX);
		return -EINVAL;
	}

	atomic_inc(&g_radio.rx_irq_count);
	g_radio.last_rx_len = frame->len;
	g_radio.last_rx_rssi_dbm = frame->rssi_dbm;
	g_radio.last_rx_rssi_raw = (u8)CLAMP((int)frame->rssi_dbm + 110, 0, 255);
	atomic_set(&g_radio.last_rx_rssi_valid, 1);
	atomic_set(&g_radio.rx_done, 1);

	if (zb_radio_extract_rx_psdu(frame->dma, frame->len, &psdu, &psdu_len) == 0) {
		zb_radio_bridge_extract_success_count++;
		zb_macDataRecvHandler((u8 *)frame->dma, (u8 *)psdu, psdu_len, 0U, 0U, frame->rssi_dbm);
		atomic_inc(&g_radio.rx_accept_count);
		return 0;
	}

	if (rf_rxBuf != NULL) {
		zb_radio_bridge_extract_fail_count++;
		saved_rx_buf = rf_rxBuf;
		rf_rxBuf = (u8 *)frame->dma;
		rf_rx_irq_handler();
		rf_rxBuf = saved_rx_buf;
		atomic_inc(&g_radio.rx_accept_count);
		return 0;
	}

	atomic_inc(&g_radio.rx_drop_count);
	return -EINVAL;
}
```

Register the sink in `zb_radio_init()`:

```c
zb_radio_port_register_rx_sink(zb_radio_consume_rx_frame);
```

Update `subsys/zigbee/platform/zephyr/zb_radio_port_tlsr8258.c`:

```c
void zb_radio_port_register_rx_sink(tlsr8258_zigbee_rx_sink_t sink)
{
	tlsr8258_zigbee_register_rx_sink(sink);
}
```

- [ ] **Step 4: Re-run Zigbee host regressions**

Run:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
tests/subsys/zigbee/host_sim/run.sh
tests/subsys/zigbee/host_shell_bootstrap/run.sh
tests/subsys/zigbee/host_app_profile/run.sh
```

Expected:

- `host_sim/run.sh` exits 0 with no bridge-queue error.
- `host_shell_bootstrap/run.sh` exits 0.
- `host_app_profile/run.sh` exits 0.

- [ ] **Step 5: Commit the bridge simplification**

Run:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
git add \
  subsys/zigbee/platform/zephyr/drv_radio_zephyr.c \
  subsys/zigbee/platform/zephyr/zb_radio_port_tlsr8258.c \
  subsys/zigbee/include/zephyr/zigbee/zb_radio_port.h \
  tests/subsys/zigbee/host_sim/run.sh \
git commit -m "zigbee: simplify tlsr8258 bridge rx sink"
```

### Task 4: Validate the new single-owner RX path on hardware

**Files:**
- Use: `zephyr/AGENTS.md`
- Verify build output: `../build-zephyr-zigbee-shell-rx-rework/zephyr/zephyr.bin`
- Inspect runtime counters in: `subsys/zigbee/platform/zephyr/drv_radio_zephyr.c`
- Inspect runtime counters in: `drivers/ieee802154/ieee802154_tlsr8258.c`

- [ ] **Step 1: Rebuild all automated checks before flashing**

Run:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
cmake -S tests/unit/tlsr8258_rx_queue -B /tmp/tlsr8258-rx-queue
cmake --build /tmp/tlsr8258-rx-queue
ctest --test-dir /tmp/tlsr8258-rx-queue --output-on-failure
cmake -S tests/unit/tlsr8258_rf_irq -B /tmp/tlsr8258-rf-irq
cmake --build /tmp/tlsr8258-rf-irq
ctest --test-dir /tmp/tlsr8258-rf-irq --output-on-failure
tests/subsys/zigbee/host_sim/run.sh
tests/subsys/zigbee/host_shell_bootstrap/run.sh
tests/subsys/zigbee/host_app_profile/run.sh
rtk ../.venv-zephyr/bin/west build -b tlsr8258_tb03f samples/zigbee/zigbee_shell \
  -d ../build-zephyr-zigbee-shell-rx-rework --pristine
```

Expected: all tests pass and the firmware build succeeds.

- [ ] **Step 2: Flash the board and clear Zigbee NVS**

Run:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
rtk python3 scripts/TlsrPgm_local.py --tcp tcp://192.168.70.44:55555 -m -w we 0 \
  ../build-zephyr-zigbee-shell-rx-rework/zephyr/zephyr.bin
rtk python3 scripts/TlsrPgm_local.py --tcp tcp://192.168.70.44:55555 -s -r es 0x7e000 0x2000
```

Expected: flash write and sector erase complete without transport errors.

- [ ] **Step 3: Open permit-join and capture the air trace**

Run the permit-join MQTT command from `zephyr/AGENTS.md`, then capture the sniffer trace:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
rtk python3 ../nrf_zigbee_sniffer.py --dev 192.168.70.48:19054 --channel 11 --duration 40 \
  --output /tmp/tlsr8258-rx-rework.pcapng
```

Expected: the capture includes `BeaconReq` from the DUT and `Beacon` / subsequent interview traffic from the coordinator.

- [ ] **Step 4: Verify that beacons now enter the Zigbee software path**

Read the known debug counters and driver state with the same RAM-read workflow already used in this worktree:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
llvm-nm -n ../build-zephyr-zigbee-shell-rx-rework/zephyr/zephyr.elf | \
  rg 'zb_radio_bridge_extract_success_count|zb_radio_bridge_beacon_count|zb_nwk_beacon_frame_count|tlsr8258_radio'
```

Then use the resolved addresses with `TlsrPgm_local.py` and `probe-rs` trace vars. Expected:

- `zb_radio_bridge_extract_success_count` increments above zero;
- `zb_radio_bridge_beacon_count` increments above zero;
- `zb_nwk_beacon_frame_count` increments above zero;
- interview no longer ends at `ZDO_NO_MATCH`.

- [ ] **Step 5: Commit the validated RX rearchitecture**

Run:

```bash
cd zephyr/.worktrees/zigbee-generic-stack
git status --short
git add \
  drivers/ieee802154/CMakeLists.txt \
  drivers/ieee802154/ieee802154_tlsr8258.c \
  drivers/ieee802154/ieee802154_tlsr8258_rx_queue.h \
  drivers/ieee802154/ieee802154_tlsr8258_rx_queue.c \
  include/zephyr/drivers/ieee802154/tlsr8258_zigbee_bridge.h \
  subsys/zigbee/include/zephyr/zigbee/zb_radio_port.h \
  subsys/zigbee/platform/zephyr/drv_radio_zephyr.c \
  subsys/zigbee/platform/zephyr/zb_radio_port_tlsr8258.c \
  tests/subsys/zigbee/host_sim/run.sh \
  tests/unit/tlsr8258_rf_irq/main.c \
  tests/unit/tlsr8258_rx_queue/CMakeLists.txt \
  tests/unit/tlsr8258_rx_queue/main.c
git commit -m "zigbee: rework tlsr8258 rx handoff"
```

Only do this final commit after the hardware checks in Steps 2-4 are satisfied.
