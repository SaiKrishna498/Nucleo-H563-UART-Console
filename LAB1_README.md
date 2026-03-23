# LAB 1 Notes - NUCLEO-H563ZI

## Goal
Build a first working firmware on `NUCLEO-H563ZI` that does:
- Blink `LD2` LED.
- React to `B1` button interrupt.
- Print status logs on UART (`USART3`) through ST-LINK VCP.

## What We Built
When firmware starts:
- LED blinks at `500 ms`.
- Pressing USER button toggles blink speed between `500 ms` and `100 ms`.
- A UART log message prints each time speed changes.

## Project Setup (CubeIDE)
Use:
- `File -> New -> STM32 Project`
- `Board Selector` -> `NUCLEO-H563ZI`

Do not use:
- `STM32 CMake Project`
- `STM32CubeIDE Empty Project`

Reason:
- A proper board project generates `.ioc`, HAL init code, and board pin labels (`LD2`, `B1`).

## .ioc Configuration Used
Main items:
- `SYS -> Debug = Serial Wire`
- `USART3 -> Asynchronous`, `115200`, `8-N-1`, no flow control
- `LD2` pin (`PF4`) = `GPIO_Output`
- `B1` pin (`PC13`) = `GPIO_EXTI13`
- `NVIC -> EXTI13_IRQn = Enabled`

Notes:
- Current button mode is `RISING` edge, so event usually occurs on release.
- If you want event on press, use `FALLING` edge.

## Key Generated/Edited Files
- `LAB_1.ioc`
- `Core/Src/main.c`
- `Core/Src/stm32h5xx_it.c`
- `Core/Inc/main.h`

## User Code Added in `main.c`
We added:
- `#include <string.h>`
- Global state:
  - `g_blink_ms` (current blink period)
  - `g_button_event` (flag set by interrupt callback)
- UART helper function:
  - `LogUart(...)` using `HAL_UART_Transmit(...)`
- EXTI callback:
  - `HAL_GPIO_EXTI_Rising_Callback(...)`
  - Toggles blink period and sets event flag.
- Main loop behavior:
  - Toggle `LD2`
  - Delay by `g_blink_ms`
  - If button event flag set, print UART message

## Interrupt Path
Button flow:
1. `PC13` changes edge.
2. `EXTI13_IRQHandler()` runs in `stm32h5xx_it.c`.
3. HAL dispatches to `HAL_GPIO_EXTI_Rising_Callback(...)`.
4. Callback updates blink speed/event flag.
5. Main loop prints log.

## Debug Issue We Hit and Fix
Problem:
- GDB server failed to bind to `61234` and SWV to `61235`.

Cause:
- Windows excluded local TCP range included those ports (`61213-61312`).

Fix in CubeIDE Debug Configuration:
- GDB server port: `61500`
- SWV port: `61501`
- Or disable SWV if not needed.

## Verification Checklist
- Build succeeds with no errors.
- Flash succeeds.
- `LD2` blinks.
- USER button changes blink speed.
- UART terminal shows:
  - Startup line
  - Speed-change lines on button events

## Useful Terminal Settings
For ST-LINK VCP serial monitor:
- Baud: `115200`
- Data bits: `8`
- Parity: `None`
- Stop bits: `1`

## Next Labs
- LAB 2: Non-blocking blink using timer interrupt instead of `HAL_Delay`.
- LAB 3: Button debounce.
- LAB 4: UART logging with interrupt or DMA.
