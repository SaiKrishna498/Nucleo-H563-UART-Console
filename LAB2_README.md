# LAB 2 Notes - Non-Blocking Blink (NUCLEO-H563ZI)

## Goal
Upgrade Lab 1 from a blocking loop (`HAL_Delay`) to a non-blocking scheduler pattern using `HAL_GetTick()`.

## Why This Matters
`HAL_Delay()` blocks CPU progress. In real embedded systems, blocking delays make it harder to:
- react quickly to events,
- run multiple tasks,
- reduce power cleanly.

Lab 2 keeps the loop responsive while preserving the same external behavior.

## Behavior
- LED `LD2` toggles periodically.
- USER button (`B1`, EXTI13) toggles blink speed (`500 ms` <-> `100 ms`).
- UART (`USART3`) prints speed-change logs.

## What Changed in Code
File:
- `Core/Src/main.c`

Key updates:
1. Added scheduler state:
   - `g_next_toggle_tick`
2. In init:
   - `g_next_toggle_tick = HAL_GetTick() + g_blink_ms;`
3. In main loop:
   - Read `now = HAL_GetTick()`
   - Toggle LED only when `now >= g_next_toggle_tick`
   - Schedule next toggle with `g_next_toggle_tick = now + g_blink_ms`
4. On button event:
   - Update blink period
   - Rebase next toggle time
   - Print UART status
5. Added:
   - `__WFI()` to idle CPU between interrupts/ticks

## Non-Blocking Pattern Used
Pseudo-flow:
1. Keep state (`next_due_time`).
2. On each loop:
   - check time,
   - run task only if due,
   - reschedule.
3. Never sleep with fixed delay in foreground loop.

## Verification
1. Build and flash.
2. Open serial monitor (`115200, 8-N-1`).
3. Confirm startup text includes:
   - `LAB2 started: non-blocking blink enabled.`
4. Press USER button and verify:
   - blink speed toggles,
   - UART prints speed messages.

## Git Checkpoint
Expected branch/tag flow:
- Branch: `lab2-nonblocking-blink`
- Tag after successful test: `lab2-done`

This gives you:
- `lab1-done` -> blocking baseline
- `lab2-done` -> non-blocking scheduler baseline

## Next Step (Lab 3)
Add debounce on `B1` so rapid edge noise does not generate repeated toggles.
