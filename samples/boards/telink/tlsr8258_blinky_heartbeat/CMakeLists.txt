# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tlsr8258_blinky_heartbeat)

target_sources(app PRIVATE src/main.c)

