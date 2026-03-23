# LAB 3 Notes - Button Debounce (NUCLEO-H563ZI)

## Goal
Add software debounce to USER button (`B1`, EXTI13) so one physical press does not cause multiple toggles.

## Why
Mechanical buttons can generate rapid edge bursts (bounce). Without debounce, one press can appear as many interrupts.

## Approach Used
Debounce in ISR callback using tick time:
- Define `BUTTON_DEBOUNCE_MS = 50`
- In `HAL_GPIO_EXTI_Rising_Callback()`:
  - Read `now = HAL_GetTick()`
  - If `now - g_last_button_tick < 50`, ignore event
  - Otherwise accept event and update `g_last_button_tick`

## Files Changed
- `Core/Src/main.c`

## Behavior After Lab 3
- LED blink remains non-blocking (Lab 2 scheduler).
- Button toggles speed (`500 ms` <-> `100 ms`) reliably.
- Spurious extra toggles from bounce are filtered.
- UART startup lines now indicate Lab 3 and debounce window.

## Test Steps
1. Build and flash firmware.
2. Open serial monitor (`115200, 8-N-1`).
3. Confirm startup lines include:
   - `LAB3 started: non-blocking blink + button debounce.`
   - `Debounce window: 50 ms.`
4. Press and release button normally:
   - one speed toggle per press/release event.
5. Try noisy/quick taps:
   - repeated events within 50 ms should be ignored.

## Tuning
If still noisy, increase debounce window:
- `50 ms` -> `75 ms` or `100 ms`.

If button feels sluggish, reduce debounce window:
- `50 ms` -> `30 ms`.
