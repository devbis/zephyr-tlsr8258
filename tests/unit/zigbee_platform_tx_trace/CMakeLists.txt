cmake_minimum_required(VERSION 3.20)

project(zigbee_platform_tx_trace C)

add_executable(zigbee_platform_tx_trace_test main.c)
target_compile_definitions(zigbee_platform_tx_trace_test PRIVATE
	WORKTREE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../..")
target_compile_features(zigbee_platform_tx_trace_test PRIVATE c_std_99)

enable_testing()
add_test(NAME zigbee_platform_tx_trace_test COMMAND zigbee_platform_tx_trace_test)
