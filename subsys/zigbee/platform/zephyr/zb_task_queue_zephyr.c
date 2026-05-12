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

static struct zb_task_item task_queue[32];
static u8 task_wptr;
static u8 task_rptr;
static struct k_spinlock task_lock;

static bool zb_taskq_pop(struct zb_task_item *item)
{
	k_spinlock_key_t key = k_spin_lock(&task_lock);

	if (task_rptr == task_wptr) {
		k_spin_unlock(&task_lock, key);
		return false;
	}

	*item = task_queue[task_rptr % ARRAY_SIZE(task_queue)];
	task_rptr++;
	k_spin_unlock(&task_lock, key);
	return true;
}

static void zb_taskq_drain(void)
{
	struct zb_task_item item;

	while (zb_taskq_pop(&item)) {
		if (item.fn != NULL) {
			item.fn(item.arg);
		}
	}
}

u8 tl_zbTaskPost(tl_zb_callback_t fn, void *arg)
{
	k_spinlock_key_t key;
	u8 next;

	if (fn == NULL) {
		return RET_INVALID_PARAMETER;
	}

	key = k_spin_lock(&task_lock);
	next = (u8)(task_wptr + 1U);
	if (next == task_rptr) {
		k_spin_unlock(&task_lock, key);
		return RET_BUSY;
	}

	task_queue[task_wptr % ARRAY_SIZE(task_queue)] = (struct zb_task_item){
		.fn = fn,
		.arg = arg,
	};
	task_wptr = next;
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
