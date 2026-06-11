# Project Notes - Runtime UART Console Configuration (NUCLEO-H563ZI)

## Goal
Build a small console on `USART3` so the board can:
- Keep printing status logs through the ST-LINK virtual COM port.
- Accept user commands from the serial terminal.
- Reconfigure the active UART settings at runtime from the same console.

## What We Built
At startup, firmware still behaves like the earlier labs:
- `LD2` blinks.
- `B1` toggles blink speed.
- UART logging stays non-blocking with interrupt-driven TX.

New project behavior:
- A command prompt appears on the UART console.
- The user can type commands such as:
  - `help`
  - `uart show`
  - `uart set 9600 8n1`
- The firmware validates the command, drains pending TX data, reinitializes `USART3`, and restarts RX interrupt handling.

## Why This Is a Good Project Step
This extends the earlier labs into something closer to a real embedded interface:
- It combines TX and RX on the same UART.
- It introduces command parsing instead of fixed-function logging only.
- It forces safe peripheral reconfiguration while firmware is running.
- It keeps the system responsive by avoiding blocking UART operations in the main application flow.

## Default UART Startup Settings
The console starts with:
- Baud: `115200`
- Data bits: `8`
- Parity: `None`
- Stop bits: `1`

Terminal setting at power-up:
- `115200, 8-N-1`

## Supported Console Commands
### `help`
Print supported commands.

### `uart show`
Print the current console UART configuration.

Example output:
- `UART console: 115200 8N1`

### `uart set <baud> <format>`
Reconfigure the active console UART.

Supported formats:
- `8n1`
- `8e1`
- `8o1`
- `8n2`

Examples:
- `uart set 9600 8n1`
- `uart set 57600 8e1`
- `uart set 115200 8n2`

Accepted baud range:
- `1200` to `1000000`

## Important Constraint
This project reconfigures the same UART link used for the console.

That means:
1. The board prints a notice before changing settings.
2. The firmware applies the new UART configuration.
3. The PC serial terminal must be changed to the same baud/format immediately.

If the terminal is left at the old setting, the console will appear broken even though the firmware is still running correctly.

## Files Changed
- `Core/Src/main.c`

## Main Design Pieces
### 1. Existing non-blocking TX log queue
The Lab 4 UART logger was kept and slightly expanded:
- `LogUart()` still queues outgoing strings.
- `HAL_UART_Transmit_IT()` sends them asynchronously.
- `HAL_UART_TxCpltCallback()` starts the next queued message.

Reason:
- Console echo, prompts, status text, and button logs can all share the same TX path.

### 2. RX interrupt capture
The firmware now receives one byte at a time with `HAL_UART_Receive_IT()`.

In the RX complete callback:
- The received byte is pushed into a small software FIFO.
- RX interrupt reception is restarted immediately for the next byte.

Reason:
- Keep the interrupt short.
- Move parsing and string handling out of ISR context.

### 3. Software FIFO for received bytes
A fixed-size FIFO stores incoming characters until the main loop processes them.

Reason:
- Prevents losing bytes while the main loop handles blink logic or queued UART output.

### 4. Line-based command parser
The main loop consumes bytes from the FIFO and builds a command line:
- Printable characters are echoed.
- Backspace removes the previous character.
- `Enter` finalizes the command.

Supported parser flow:
1. Collect a full line.
2. Tokenize the line.
3. Validate command and parameters.
4. Execute the command or print usage text.

### 5. Safe UART reconfiguration sequence
When `uart set` is accepted, the firmware does this in order:
1. Print `Applying UART console: ...`
2. Wait for all queued TX data to finish
3. Abort RX interrupt activity
4. Deinitialize `USART3`
5. Update baud/format fields in `huart3.Init`
6. Reinitialize the UART peripheral
7. Restore FIFO thresholds and FIFO mode settings
8. Restart RX interrupt reception

Reason:
- Avoid changing UART settings while bytes are still being transmitted.
- Keep the peripheral state coherent after reconfiguration.

## Data Flow
### TX path
1. Application calls `LogUart("message")`
2. Message is copied into the TX queue
3. If UART is idle, `HAL_UART_Transmit_IT()` starts transmission
4. TX complete interrupt advances the queue

### RX path
1. User types a character in the serial terminal
2. `USART3` RX interrupt fires
3. `HAL_UART_RxCpltCallback()` stores the byte in the RX FIFO
4. Main loop pops bytes from the FIFO
5. Main loop echoes characters and builds a command line
6. Completed line is parsed and executed

## Error Handling and Limits
Current implementation includes:
- RX overflow flag if the receive FIFO fills up
- Command validation for bad baud values
- Command validation for unsupported UART formats

Current limits:
- Fixed RX FIFO depth
- Fixed command line length
- Only a small set of UART formats is supported
- Settings are not yet saved to flash

## Test Procedure
1. Build the project.
2. Flash the firmware to the `NUCLEO-H563ZI`.
3. Open a serial terminal on the ST-LINK VCP with `115200, 8-N-1`.
4. Reset the board.
5. Verify startup output includes the console command hint.
6. Type:
   - `help`
   - `uart show`
7. Type:
   - `uart set 9600 8n1`
8. Change the terminal to `9600, 8-N-1`.
9. Verify the console resumes and `uart show` reports the new setting.
10. Repeat with another valid format such as:
   - `uart set 115200 8e1`
11. Change the terminal again to match.

## Expected Results
- The board continues blinking and responding to the USER button.
- The console echoes typed characters.
- Valid commands execute correctly.
- Invalid commands print usage or error text.
- UART settings change only after pending TX output is drained.

## Known Tradeoffs
- Reconfiguring the same console UART is useful for learning, but it is awkward for end users because the host terminal must follow every change.
- Per-character echo creates more UART traffic than a silent command parser.
- The implementation is intentionally simple and single-console only.

## Good Next Improvements
- Add separate commands for parity, stop bits, and baud if finer control is needed.
- Store UART settings in flash and restore them on boot.
- Add a `factory reset` command to return to `115200 8N1`.
- Add a dropped-message counter for the TX queue.
- Move from one-byte RX interrupts to DMA + idle-line detection for heavier console traffic.

## Summary
This project turns the earlier UART logging work into an interactive embedded console. The firmware now supports runtime UART reconfiguration through serial commands while preserving non-blocking logging and existing blink/button behavior. It is a solid step from lab exercises toward a practical embedded control interface.
