# Implementation Status

Gate Link is code-complete through command authentication and replay
resistance. The LED-only command chain and robustness paths were bench-validated
before authentication was added. The authentication and replay logic are
host-tested, and the normal authenticated LoRa command/ACK flow has been
validated on the ESP32 bench pair with a local shared key.

Authentication proves who sent a packet and rejects sequences the receiver has
already passed. It does not make a captured-but-undelivered command unusable;
see the jam-and-replay limit in `SECURITY.md` before installing this anywhere
that matters.

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
Phase 8   Security/authentication           code complete, bench partial
Phase 9   Range test                        pending
Phase 10  Real actuator preparation         pending
```

Phase 7 is complete for the LED-only bench setup. It does not enable a hardware
watchdog yet; see D013 in `docs/decisions.md`.

Phase 8 is implemented and host-tested, and the normal authenticated flow runs
on the bench pair. It is not marked done because the paths that only matter when
something goes wrong have not been exercised on hardware yet: duplicate handling
after a lost ACK, replay rejection, and a bad tag being dropped. Those are
TEST-003, TEST-011, and TEST-012 in `docs/test-plan.md`. Until they run against
the authenticated firmware, the evidence is the twister suites, not the bench.

## Verified on the Bench

Observed with the authenticated application firmware running on both boards:

- both ESP32 boards enumerate over USB serial;
- both boards boot Zephyr;
- both LoRa modules answer with SX1276 version `0x12`;
- TX and RX load their NVS counter state at boot;
- TX sends fixed-size authenticated binary protocol packets with monotonic
  persisted sequences;
- RX receives, validates, and authenticates those packets;
- RX sends authenticated ACK packets for valid commands;
- TX authenticates the matching ACK and reports command success;
- bench RSSI/SNR is visible in serial logs.

Observed with the pre-authentication application firmware running on both
boards:

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
  on the next button press after the module answers again.

The GPIO4 button and the GPIO25/GPIO33 LEDs were first validated electrically
with a temporary I/O sketch and a logic analyzer. They are now also validated
through the application firmware in TEST-001, TEST-002, and TEST-004.

## Covered by Host Tests

- invalid, short, long, and reserved-field frames;
- ACK matching on device id, sequence, and command;
- HMAC-SHA256 tag generation against a known vector;
- authentication rejection for tampered fields, tampered tags, and wrong keys;
- authenticated replay filtering, including duplicate ACK behavior, stale
  sequence rejection, per-device state, counter wrap rejection, and the rule
  that asking the filter does not record a sequence before explicit accept.

## Remaining Bench Gaps

The receiver retry path for a stuck actuator output is implemented but has not
been physically forced on the bench. The robustness tests that intentionally
drop ACKs, remove the receiver, or power-cycle radio modules were validated
before authentication and should be rerun with the local shared key before range
testing.

The specific gap worth closing first is TEST-003, duplicate handling after a
lost ACK. Under the replay filter it is now the path that decides whether a lost
ACK costs one extra button press or none: if `GATE_REPLAY_DECISION_DUPLICATE`
misbehaved, every retransmission would be rejected as a replay and the gate
would need a second press whenever an ACK is lost. Twister exercises the filter;
nothing has yet exercised it on hardware with real NVS state. The old method of
producing a duplicate — resetting only the transmitter — no longer works, since
the transmitter now persists its counter. Jamming the ACK is the way: cover the
receiver antenna, or unplug the receiver, immediately after the actuator pulse.

## Current Firmware Behavior

The transmitter waits for one debounced physical button press. Each accepted
press reserves and persists a new monotonic sequence, creates one authenticated
`TRIGGER` command, and sends it. Holding the button down does not create another
command; the transmitter waits for a debounced release before returning to
ready state. If no authenticated matching ACK arrives before
`CONFIG_GATE_TX_ACK_TIMEOUT_MS`, the transmitter sends the same packet again
until `CONFIG_GATE_TX_MAX_RETRIES` is exhausted.

The receiver accepts only valid `COMMAND(TRIGGER)` packets carrying the
transmitter id in `CONFIG_GATE_RX_ACCEPTED_DEVICE_ID` and a valid HMAC tag. For
a sequence greater than the last accepted one, it triggers the actuator output,
records the sequence as accepted, persists it, and only then sends an
authenticated ACK for the same device id, sequence, and command. For a
retransmitted sequence already accepted from that same transmitter identity, it
logs that the actuator was suppressed and sends ACK again. Older authenticated
sequences are rejected as replay. Commands from any other device id are dropped
without an ACK.

The sequence is recorded only once the actuator pulse completes, so a pulse that
never fired is neither acknowledged nor remembered, and the transmitter's retry
is treated as a fresh command rather than suppressed as a duplicate. If the
sequence cannot be persisted after a successful pulse, ACK is withheld and the
receiver retries the storage write instead of accepting new commands. Failing to
lower the output afterwards is treated differently from failing to raise it: the
receiver retries indefinitely rather than returning to listening with the output
still energized (D013, D018).

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

Next implementation work is authenticated robustness retest followed by
LED-only range testing. Range testing must happen before any real gate actuator
is connected.

Manual validation steps are kept in `docs/test-plan.md`.
