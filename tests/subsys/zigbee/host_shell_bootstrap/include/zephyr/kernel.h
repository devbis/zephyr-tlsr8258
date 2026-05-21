#ifndef ZEPHYR_KERNEL_H_
#define ZEPHYR_KERNEL_H_

#include <stdbool.h>
#include <stdint.h>

#define ARG_UNUSED(x) (void)(x)
#define K_SECONDS(x) (x)
#define K_MSEC(x) (x)

struct k_work {
	int unused;
};

struct k_work_delayable {
	void (*handler)(struct k_work *work);
	bool scheduled;
};

static inline void k_work_init_delayable(struct k_work_delayable *work,
					 void (*handler)(struct k_work *work_item))
{
	work->handler = handler;
	work->scheduled = false;
}

static inline int k_work_reschedule(struct k_work_delayable *work, int delay)
{
	ARG_UNUSED(delay);
	work->scheduled = true;
	return 0;
}

static inline int k_work_schedule(struct k_work_delayable *work, int delay)
{
	return k_work_reschedule(work, delay);
}

static inline int k_work_cancel_delayable(struct k_work_delayable *work)
{
	work->scheduled = false;
	return 0;
}

#endif
