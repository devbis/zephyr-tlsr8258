# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

board_runner_args(tlsrpgm "--dt-flash=y")
include(${ZEPHYR_BASE}/boards/common/tlsrpgm.board.cmake)
