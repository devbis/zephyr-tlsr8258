cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zigbee_shell_bootstrap)

target_sources(app PRIVATE
	src/main.c
	${ZEPHYR_BASE}/samples/zigbee/zigbee_shell/src/main.c
)

target_compile_definitions(app PRIVATE
	main=zigbee_shell_app_main
	CONFIG_ZIGBEE_BDB=1
)

target_include_directories(app PRIVATE
	${ZEPHYR_BASE}/subsys/zigbee/include
)
