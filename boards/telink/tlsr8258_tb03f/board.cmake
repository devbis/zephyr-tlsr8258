# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

board_runner_args(probe-rs "--chip=TLSR8258" "--file-type=bin" "--dt-flash=y")
board_runner_args(tlsrpgm "--dt-flash=y")
include(${ZEPHYR_BASE}/boards/common/probe-rs.board.cmake)
include(${ZEPHYR_BASE}/boards/common/tlsrpgm.board.cmake)
