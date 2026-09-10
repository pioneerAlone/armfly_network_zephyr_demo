# SPDX-License-Identifier: Apache-2.0

# Prefer STM32CubeProgrammer for flash operations with ST-LINK/V2+ over SWD.
board_set_flasher_ifnset(stm32cubeprogrammer)

board_runner_args(stm32cubeprogrammer "--port=swd" "--reset-mode=hw")
board_runner_args(jlink "--device=STM32F429BI" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/stm32cubeprogrammer.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
