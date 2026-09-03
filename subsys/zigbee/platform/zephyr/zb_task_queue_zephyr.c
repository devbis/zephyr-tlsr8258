/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "tl_platform.h"
#include "zb_common_stub.h"
#include "ev_poll.h"

extern struct k_sem zb_ev_sem;

struct zb_task_item {
	tl_zb_callback_t fn;
	void *arg;
};

/*
 * RX delivery can arrive in a burst while the Zigbee thread is draining
 * MAC/NWK primitives.  A shared depth of 32 allowed the radio callback to
 * ACK an AssocResp and then lose its deferred mac_rxDataParse callback when
 * unrelated timer/TX callbacks filled the queue.  Keep RX delivery on its
 * own FIFO so interview and post-join encrypted frames cannot be starved by
 * control callbacks.
 */
#define ZB_TASK_POST_QUEUE_DEPTH 64U
#define ZB_TASK_RX_QUEUE_DEPTH 64U
/* A callback is allowed to post another callback.  Draining until the queue
 * is empty therefore has no progress bound: a periodic/timer callback can
 * keep the ZB thread in this function forever while the RF ISR still emits
 * MAC ACKs.  Leave a bounded slice for RX polling, NWK dispatch and the
 * always-RX guard on every loop iteration. */
#define ZB_TASK_DRAIN_BUDGET 32U

/* Public vendor ABI used by mac_trx.c for queue back-pressure. The Zephyr
 * callback lane has the same fixed capacity as the vendor user-task queue. */
u8 ZB_TASKQ_USERUSE_SIZE = ZB_TASK_POST_QUEUE_DEPTH;

static struct zb_task_item task_queue[ZB_TASK_POST_QUEUE_DEPTH];
static u8 task_wptr;
static u8 task_rptr;
static u8 task_count;
static struct zb_task_item rx_task_queue[ZB_TASK_RX_QUEUE_DEPTH];
static u8 rx_task_wptr;
static u8 rx_task_rptr;
static u8 rx_task_count;
static struct k_spinlock task_lock;

static bool zb_taskq_pop(struct zb_task_item *item)
{
	k_spinlock_key_t key = k_spin_lock(&task_lock);

	/* RX parsing has priority: it is the only path that can turn a received
	 * frame into a MAC/NWK response before the coordinator's retry window. */
	if (rx_task_count != 0U) {
		*item = rx_task_queue[rx_task_rptr];
		rx_task_rptr = (u8)((rx_task_rptr + 1U) % ARRAY_SIZE(rx_task_queue));
		rx_task_count--;
		k_spin_unlock(&task_lock, key);
		return true;
	}

	if (task_count == 0U) {
		k_spin_unlock(&task_lock, key);
		return false;
	}

	*item = task_queue[task_rptr];
	task_rptr = (u8)((task_rptr + 1U) % ARRAY_SIZE(task_queue));
	task_count--;
	k_spin_unlock(&task_lock, key);
	return true;
}

static void zb_taskq_drain(void)
{
	struct zb_task_item item;
	u8 budget = ZB_TASK_DRAIN_BUDGET;

	while (budget-- != 0U && zb_taskq_pop(&item)) {
		if (item.fn != NULL) {
			item.fn(item.arg);
		}
	}

	/* Keep the outgoing NWK security counter ahead of a reboot.  This is
	 * the Zephyr equivalent of the vendor tl_zbTaskProcedure() tail call;
	 * zdo_ssInfoUpdate() persists the initial counter range after joining. */
	zdo_ssInfoUpdate();
}

u8 tl_zbRxTaskPost(tl_zb_callback_t fn, void *arg)
{
	k_spinlock_key_t key;
	u8 next;

	if (fn == NULL) {
		return RET_INVALID_PARAMETER;
	}

	key = k_spin_lock(&task_lock);
	if (rx_task_count == ARRAY_SIZE(rx_task_queue)) {
		k_spin_unlock(&task_lock, key);
		return RET_BUSY;
	}

	rx_task_queue[rx_task_wptr] = (struct zb_task_item){
		.fn = fn,
		.arg = arg,
	};
	next = (u8)((rx_task_wptr + 1U) % ARRAY_SIZE(rx_task_queue));
	rx_task_wptr = next;
	rx_task_count++;
	k_spin_unlock(&task_lock, key);
	k_sem_give(&zb_ev_sem);
	return RET_OK;
}

u8 tl_zbTaskPost(tl_zb_callback_t fn, void *arg)
{
	k_spinlock_key_t key;
	u8 next;

	if (fn == NULL) {
		return RET_INVALID_PARAMETER;
	}

	key = k_spin_lock(&task_lock);

	if (task_count == ARRAY_SIZE(task_queue)) {
		k_spin_unlock(&task_lock, key);
		return RET_BUSY;
	}

	task_queue[task_wptr] = (struct zb_task_item){
		.fn = fn,
		.arg = arg,
	};
	next = (u8)((task_wptr + 1U) % ARRAY_SIZE(task_queue));
	task_wptr = next;
	task_count++;
	k_spin_unlock(&task_lock, key);

	k_sem_give(&zb_ev_sem);
	return RET_OK;
}

void zb_taskq_run_pending_for_test(void)
{
	zb_taskq_drain();
}

static int zb_taskq_init(void)
{
	ev_on_poll(EV_POLL_HCI, zb_taskq_drain);
	return 0;
}

SYS_INIT(zb_taskq_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
