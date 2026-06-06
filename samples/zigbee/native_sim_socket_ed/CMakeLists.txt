# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(native_sim_socket_ed)

target_sources(app PRIVATE
	src/main.c
	src/app_bdb.c
	src/app_profile.c
)

target_include_directories(app PRIVATE
	${ZEPHYR_BASE}/subsys/zigbee
	${ZEPHYR_BASE}/subsys/zigbee/include
	${ZEPHYR_BASE}/subsys/zigbee/platform/zephyr
)
