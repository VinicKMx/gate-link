# Implementation Status

Gate Link is code-complete and bench-validated through Phase 7 on the current
bench hardware:
physical button, status LEDs, LoRa command/ACK, retry, duplicate suppression,
and software robustness paths are implemented and have passed the manual tests
recorded in `docs/test-plan.md`.

This file tracks the public technical state of the firmware.

## Bench Hardware

Current bench target:

```text
ESP32 DevKitC V4 WROOM-32D
HopeRF RFM95W 915 MHz / SX1276
Zephyr RTOS
LoRa point-to-point
```

Current RFM95W wiring:

```text
RFM95W      ESP32
VCC/3V3  -> 3V3
GND      -> GND
SCK      -> GPIO18
MISO     -> GPIO19
MOSI     -> GPIO23
NSS/CS   -> GPIO5
RESET    -> GPIO17
DIO0     -> GPIO16
```

The GPIO5 chip-select line is controlled by the ESP32 SPI hardware, not by
`cs-gpios`; see D011 in `docs/decisions.md`.

## Phase Checklist

```text
Phase 0   Repository bootstrap              done
Phase 1   GPIO local                        done
Phase 2   Minimal LoRa communication        done
Phase 3   Structured protocol               done
Phase 4   ACK                               done
Phase 5   Timeout and retry                 done
Phase 6   Duplicate suppression             done
Phase 7   Robustness                        done
Phase 8   Security/authentication           pending
Phase 9   Range test                        pending
Phase 10  Real actuator preparation         pending
```

Phase 7 is complete for the LED-only bench setup. It does not enable a hardware
watchdog yet; see D013 in `docs/decisions.md`.

## Verified on the Bench

Observed with the application firmware running on both boards:

- both ESP32 boards enumerate over USB serial;
- both boards boot Zephyr;
- both LoRa modules answer with SX1276 version `0x12`;
- TX sends fixed-size binary protocol packets;
- RX receives and validates those packets;
- RX sends ACK packets for valid commands;
- TX accepts the matching ACK and reports command success;
- TX retries the same sequence after ACK timeout;
- TX stops after a configured retry limit and reports final failure;
- RX suppresses duplicate command execution for the accepted transmitter id;
- RX still sends ACK when a valid duplicate command is received;
- TX turns one debounced physical press into exactly one command;
- TX ignores a held button until it is released;
- TX success and error LEDs reflect command outcome;
- RX produces one actuator pulse per non-duplicate command and no pulse for a
  duplicate;
- RX detects a powered-down RFM95W during receive timeouts, retries radio
  recovery, and receives commands again after the module power returns;
- TX recovers immediately after a local radio send failure and sends normally
  on the next button press after the module answers again;
- bench RSSI/SNR is visible in serial logs.

The GPIO4 button and the GPIO25/GPIO33 LEDs were first validated electrically
with a temporary I/O sketch and a logic analyzer. They are now also validated
through the application firmware in TEST-001, TEST-002, and TEST-004.

## Covered by Host Tests

- TX sequence wraparound and TX reboot-style sequence restart;
- invalid, short, long, and reserved-field frames;
- ACK matching on device id, sequence, and command;
- RX duplicate state, RX reset behavior, and the rule that a sequence is
  recorded only after the actuator pulse completes.

## Remaining Bench Gaps

The receiver retry path for a stuck actuator output is implemented but has not
been physically forced on the bench. The remaining validation before real
actuator hardware is range and installation testing, not the LED-only command
chain.

## Current Firmware Behavior

The transmitter waits for one debounced physical button press. Each accepted
press creates one `TRIGGER` command with a new sequence number. Holding the
button down does not create another command; the transmitter waits for a
debounced release before returning to ready state. If no matching ACK arrives
before `CONFIG_GATE_TX_ACK_TIMEOUT_MS`, the transmitter sends the same packet
again until `CONFIG_GATE_TX_MAX_RETRIES` is exhausted.

The receiver accepts only valid `COMMAND(TRIGGER)` packets carrying the
transmitter id in `CONFIG_GATE_RX_ACCEPTED_DEVICE_ID`. For a new sequence, it
triggers the actuator output, records the sequence as accepted, and sends an
ACK for the same device id, sequence, and command. For a retransmitted sequence
already accepted from that same transmitter identity, it logs that the actuator
was suppressed and sends ACK again. Commands from any other device id are
dropped without an ACK. That filter is addressing, not authentication; phase 8
is what makes a command unforgeable.

The sequence is recorded only once the actuator pulse completes, so a pulse that
never fired is neither acknowledged nor remembered, and the transmitter's retry
is treated as a fresh command rather than suppressed as a duplicate. Failing to
lower the output afterwards is treated differently from failing to raise it: the
receiver retries indefinitely rather than returning to listening with the output
still energized (D013).

Frames longer than the 19-byte packet are rejected at the radio boundary rather
than truncated, so an oversized frame can never be shortened into something
that passes packet validation.

The transmitter switches to RX after each send and reports success only when
the decoded ACK matches the device id, sequence, and command currently in
progress. Non-matching packets are ignored.

Both applications treat a missing or failing radio as recoverable. At boot they
retry the probe every `CONFIG_GATE_RADIO_RECOVERY_INTERVAL_MS` until the module
answers. At runtime, the receiver uses
`CONFIG_GATE_RADIO_RECOVERY_THRESHOLD` to avoid re-probing after a single
unexpected radio error, and it always returns to RX mode after recovery. The
receiver also refreshes RX mode after receive timeouts, because a powered-down
SX1276 can otherwise look like an idle link rather than a hard radio error.
The transmitter recovers immediately after a local radio failure during a
command, so a TX module that loses power while idle does not require multiple
button presses to heal. An unanswered command is a link failure, not a radio
failure, and never triggers transmitter recovery. A local packet construction
error is also not recovered through the radio. A build with no radio configured
at all idles on purpose (D012, D015, D016).

## Next Criteria

Next implementation phase is Phase 8: command authentication and replay
resistance. Range testing remains LED-only and must happen before any real gate
actuator is connected.

Manual validation steps are kept in `docs/test-plan.md`.
