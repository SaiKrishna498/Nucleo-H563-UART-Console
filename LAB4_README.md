# LAB 4 Notes - UART Logging with Interrupt Queue (NUCLEO-H563ZI)

## Goal
Move UART logging from blocking API (`HAL_UART_Transmit`) to non-blocking interrupt mode (`HAL_UART_Transmit_IT`) so the main loop stays responsive.

## What Changed
Files:
- `Core/Src/main.c`
- `Core/Src/stm32h5xx_it.c`
- `Core/Inc/stm32h5xx_it.h`

Key additions:
1. UART log queue in `main.c`
   - Fixed-depth queue: `UART_LOG_QUEUE_DEPTH`
   - Fixed message size: `UART_LOG_MSG_MAX_LEN`
2. Non-blocking logger flow
   - `LogUart()` enqueues message
   - `UartLogStartTxIfIdle()` starts TX with `HAL_UART_Transmit_IT()`
   - `HAL_UART_TxCpltCallback()` advances queue and starts next message
3. USART3 IRQ wiring
   - Enable NVIC in `MX_USART3_UART_Init()`
   - Add `USART3_IRQHandler()` and call `HAL_UART_IRQHandler(&huart3)`

## Why This Is Better
- No blocking time in `LogUart()`
- Main loop remains responsive for LED timing and button events
- Scales better when log frequency increases

## Data Flow
1. App calls `LogUart("message")`
2. Message copied into queue
3. If UART idle, start interrupt TX
4. On TX complete interrupt:
   - dequeue current message
   - automatically start next queued message

## Notes
- Queue is intentionally simple and fixed-size.
- If queue is full, new message is dropped.
- Existing Lab 2 (non-blocking blink) and Lab 3 (debounce) behavior is preserved.

## Test Steps
1. Build and flash.
2. Open serial monitor (`115200, 8-N-1`).
3. Confirm startup lines:
   - `LAB4 started: non-blocking UART logging with interrupt queue.`
   - `UART mode: HAL_UART_Transmit_IT (USART3 IRQ).`
4. Press USER button repeatedly.
5. Verify:
   - Blink speed toggles correctly.
   - UART messages keep printing without freezing the loop.

## Next Improvement Ideas
- Use DMA for larger burst logs.
- Add dropped-message counter for queue-full visibility.
- Move queue to lock-free or RTOS-safe structure when tasks are introduced.
