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

ZTEST_SUITE(task_post_queue, NULL, NULL, NULL, NULL, NULL);
