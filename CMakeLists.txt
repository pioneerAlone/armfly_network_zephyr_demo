# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

target_sources(app PRIVATE
	src/main.c
	src/app_startup.c
	src/app_logging.c
	src/app_time.c
	src/at_control_service.c
	src/modem_device.c
	src/mqtt_tls_credentials.c
	src/ppp_session.c
	src/mqtt_tls_client.c
)
