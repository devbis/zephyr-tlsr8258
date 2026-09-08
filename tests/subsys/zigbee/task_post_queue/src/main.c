#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include "zb_common_stub.h"

extern void zb_taskq_run_pending_for_test(void);

K_SEM_DEFINE(zb_ev_sem, 0, 1);

static int called;

static void test_cb(void *arg)
{
	called += POINTER_TO_INT(arg);
}

ZTEST(task_post_queue, test_task_post_executes_callback)
{
	called = 0;
	zassert_equal(tl_zbTaskPost(test_cb, INT_TO_POINTER(3)), RET_OK);
	zb_taskq_run_pending_for_test();
	zassert_equal(called, 3);
}

ZTEST(task_post_queue, test_task_post_queue_full_and_wrap)
{
	const int queue_slots = 32;

	called = 0;
	for (int i = 1; i <= queue_slots; i++) {
		zassert_equal(tl_zbTaskPost(test_cb, INT_TO_POINTER(i)), RET_OK);
	}

	zassert_equal(tl_zbTaskPost(test_cb, INT_TO_POINTER(99)), RET_BUSY);
	zb_taskq_run_pending_for_test();
	zassert_equal(called, ((queue_slots * (queue_slots + 1)) / 2));

	zassert_equal(tl_zbTaskPost(test_cb, INT_TO_POINTER(5)), RET_OK);
	zb_taskq_run_pending_for_test();
	zassert_equal(called, ((queue_slots * (queue_slots + 1)) / 2) + 5);
}

ZTEST_SUITE(task_post_queue, NULL, NULL, NULL, NULL, NULL);
