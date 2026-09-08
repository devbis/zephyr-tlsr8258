cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(task_post_queue)

set(ZIGBEE_SUBSYS_DIR ${ZEPHYR_BASE}/subsys/zigbee)

target_sources(app PRIVATE
	src/main.c
	${ZIGBEE_SUBSYS_DIR}/platform/zephyr/ev_poll_zephyr.c
	${ZIGBEE_SUBSYS_DIR}/platform/zephyr/zb_task_queue_zephyr.c
)

target_include_directories(app PRIVATE
	${ZIGBEE_SUBSYS_DIR}
	${ZIGBEE_SUBSYS_DIR}/platform/zephyr
	${ZIGBEE_SUBSYS_DIR}/os
	${ZIGBEE_SUBSYS_DIR}/common
	${ZIGBEE_SUBSYS_DIR}/mac/includes
	${ZIGBEE_SUBSYS_DIR}/zbapi
	${ZIGBEE_SUBSYS_DIR}/include
)
