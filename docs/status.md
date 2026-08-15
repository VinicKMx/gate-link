# Implementation Status

Gate Link has completed Phase 4: structured LoRa packets with ACK. Phase 5,
timeout plus retry, is next.

This file tracks the public technical state of the firmware. Local planning
notes and working history stay outside version control.

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
Phase 1   GPIO local                        pending hardware wiring
Phase 2   Minimal LoRa communication        done
Phase 3   Structured protocol               done
Phase 4   ACK                               done
Phase 5   Timeout and retry                 next
Phase 6   Duplicate suppression             pending
Phase 7   Robustness                        pending
Phase 8   Security/authentication           pending
Phase 9   Range test                        pending
Phase 10  Real actuator preparation         pending
```

Phase 1 is marked as pending because the physical button, LEDs, resistors, and
capacitors are not wired yet. The firmware keeps the devicetree aliases in
place so GPIO validation can be added without changing the radio/protocol
modules.

## Verified So Far

- both ESP32 boards enumerate over USB serial;
- both boards boot Zephyr;
- both LoRa modules answer with SX1276 version `0x12`;
- TX sends fixed-size binary protocol packets;
- RX receives and validates those packets;
- RX sends ACK packets for valid commands;
- TX accepts the matching ACK and reports command success;
- bench RSSI/SNR is visible in serial logs.

## Current Firmware Behavior

Until the physical button is connected, the transmitter sends a bench
`TRIGGER` command at a configured interval. Each command gets a new sequence
number.

The receiver accepts only valid `COMMAND(TRIGGER)` packets carrying the
transmitter id in `CONFIG_GATE_RX_ACCEPTED_DEVICE_ID`, and sends an ACK for the
same device id, sequence, and command. Commands from any other device id are
dropped without an ACK. That filter is addressing, not authentication; phase 8
is what makes a command unforgeable.

Frames longer than the 19-byte packet are rejected at the radio boundary rather
than truncated, so an oversized frame can never be shortened into something
that passes packet validation.

The transmitter switches to RX after sending and reports success only when the
decoded ACK matches the sequence currently in progress. Non-matching packets
are ignored.

## Next Criteria

Phase 5 is complete when:

- ACK timeout is explicit in TX logs;
- TX retransmits the same sequence after timeout;
- maximum retry count is configurable;
- TX reports final failure after the retry limit;
- RX still sends ACK for valid commands.

Phase 6 is complete when a retransmitted command with the same sequence causes
the receiver to send another ACK but does not trigger the actuator twice.
