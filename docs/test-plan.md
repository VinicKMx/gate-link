# Test Plan

This document tracks manual bench validation for the current LED-only build.
Hardware-independent protocol and sequence behavior is covered by ztest under
`tests/`.

## Results

A test counts as passed only after it has been run against the application
firmware on the bench. Running the temporary I/O test sketch does not count: it
proves the wiring, not this firmware.

| Test     | Subject                       | Last run   | Result      |
|----------|-------------------------------|------------|-------------|
| TEST-001 | Normal trigger flow           | -          | not run     |
| TEST-002 | Button held                   | -          | not run     |
| TEST-003 | Lost ACK / duplicate command  | -          | not run     |
| TEST-004 | Receiver unavailable          | -          | not run     |
| TEST-005 | Invalid or oversized packet   | -          | twister     |
| TEST-006 | Incorrect ACK                 | -          | twister     |
| TEST-007 | Radio failure recovery        | -          | not run     |

TEST-005 and TEST-006 run under twister rather than on the bench; fill in the
date of the last passing run. Update this table in the same commit that runs a
test, so `docs/status.md` never claims more than this table supports.

## TEST-001 - Normal Trigger Flow

Given:

- TX and RX are powered over USB;
- both RFM95W modules are connected with antennas;
- TX button and status LEDs are wired;
- RX actuator and status LEDs are wired.

When:

- the TX button is pressed once and released.

Then:

- TX logs one debounced button press;
- TX sends one `COMMAND(TRIGGER)` with a new sequence number;
- RX logs the command;
- RX pulses the actuator LED once;
- RX sends an ACK for the same sequence;
- TX logs the matching ACK and lights the success LED;
- TX error LED remains off.

## TEST-002 - Button Held

When:

- the TX button is held down for several seconds.

Then:

- TX emits only one command for that hold;
- TX logs that it is waiting for button release;
- no second command is sent until the button is released and pressed again.

## TEST-003 - Lost ACK / Duplicate Command

When:

- the ACK for a command is lost or the TX times out while waiting for it.

Then:

- TX retransmits the same sequence;
- RX logs the repeated sequence as a duplicate;
- RX sends ACK again;
- RX does not pulse the actuator LED again for that duplicate.

## TEST-004 - Receiver Unavailable

When:

- RX is powered off or out of range;
- the TX button is pressed once.

Then:

- TX attempts the initial send plus `CONFIG_GATE_TX_MAX_RETRIES` retries;
- TX logs a final failure;
- TX lights the error LED;
- TX returns to waiting for button release and then to ready state.

## TEST-005 - Invalid or Oversized Packet

Given:

- protocol host tests are run with twister.

Then:

- short, long, unknown-version, unknown-type, unknown-command, reserved-device,
  and reserved-sequence packets are rejected;
- oversized radio frames are rejected before decode.

## TEST-006 - Incorrect ACK

Given:

- protocol host tests are run with twister.

Then:

- an ACK only matches when device id, sequence, and command all match the
  command currently in progress;
- stale ACKs, unrelated ACKs, and command echoes are ignored.

## TEST-007 - Radio Failure Recovery

When:

- the radio is unavailable at boot or returns repeated runtime errors.

Then:

- firmware does not terminate the main loop;
- logs show paced recovery attempts;
- RX re-enters receive mode after recovery;
- TX can send again after recovery.
