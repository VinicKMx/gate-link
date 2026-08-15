# Protocol

The protocol is binary and command-oriented. It does not depend on packet strings
such as `OPEN`.

## Message Types

```text
COMMAND
ACK
```

## Commands

```text
TRIGGER
```

`TRIGGER` means "produce one actuator pulse". It deliberately does not describe
the final gate movement.

## Packet Fields

Every packet is a fixed 19-byte little-endian frame:

```text
offset  size  field
0       1     protocol_version
1       1     message_type
2       4     device_id
6       4     sequence
10      1     command
11      8     auth_tag
```

Both `COMMAND` and `ACK` use this shape. An `ACK` echoes the command it
acknowledges, so validation is uniform for every message type.

`auth_tag` is part of the packet model and carries the packet authentication
tag.

Current authenticated packets use:

```text
auth_tag = first 8 bytes of HMAC-SHA256(key, packet[0..10])
```

The authenticated bytes are `protocol_version`, `message_type`, `device_id`,
`sequence`, and `command`. The `auth_tag` field itself is zeroed while the tag
is calculated. Both `COMMAND` and `ACK` packets are authenticated; a forged ACK
must not make the transmitter report success.

Two values are reserved and never valid on the wire:

```text
device_id == 0    device identity not assigned
sequence  == 0    no command in progress
```

Reserving them lets an uninitialized packet be rejected as invalid instead of
travelling, and gives the receiver a distinct "nothing executed yet" state for
duplicate detection.

A frame is structurally valid only when the length is exactly 19 bytes, the
version matches, the message type and command are known values, and neither
reserved value appears. A frame failing any check is dropped without touching
decoded output, so a caller can never act on a partially parsed packet.

After structural validation, applications authenticate the packet before
changing state, triggering the actuator, or accepting an ACK.

## Sequence Numbers

Each command has a sequence number assigned by the transmitter.

The sequence number is used to:

- match an ACK to the command currently waiting for confirmation;
- distinguish a new command from a retransmission;
- prevent duplicate actuator pulses when an ACK is lost.
- reject older authenticated packets as replay.

The transmitter stores the last issued sequence in NVS before sending the
packet. If a send fails or the transmitter loses power after reserving a
sequence, that number is consumed. Skipping a number is acceptable; reusing one
is not.

Authenticated operation does not wrap from `UINT32_MAX` back to `1`. Exhausting
the counter requires reprovisioning the pair. That is a deliberate tradeoff: a
wrap would make old captured packets numerically fresh again.

## ACK Rules

The receiver sends `ACK(sequence)` after accepting a valid command packet.

The transmitter reports success only when:

```text
message_type == ACK
sequence == current_command_sequence
device_id == current_command_device_id
command == current_command
packet validation succeeds
packet authentication succeeds
```

ACKs for old, unknown, wrong-command, or unrelated sequences are ignored.
ACKs with invalid authentication tags are ignored before ACK matching.

In protocol version 1, the ACK echoes the transmitter `device_id`, `sequence`,
and `command` from the command being acknowledged. This keeps ACK matching tied
to the command currently in progress without adding a receiver identity field
to the initial packet shape.

## Retransmission Rules

When the transmitter does not receive a matching ACK before the configured ACK
timeout, it retransmits the same `COMMAND` packet with the same sequence number.
A retry is not a new command.

The transmitter gives up after the configured retry limit and reports final
failure for that sequence. The next command receives the next sequence number.

The receiver fires the actuator before it sends the ACK, so the actuator pulse
is part of the turnaround the transmitter is waiting on. The ACK timeout must
therefore stay comfortably above the pulse duration plus airtime. If it does
not, every command times out and the transmitter reports failure for commands
the receiver actually executed, which is the most misleading state this
protocol can reach: the gate opens and the user is told it did not.

A sequence is recorded as accepted only after the actuator pulse completes.
Failing to fire leaves the sequence unrecorded and unacknowledged, so the
transmitter's retry is treated as a fresh command rather than a duplicate.

## Duplicate and Replay Rules

The receiver tracks the last accepted command sequence per accepted transmitter
identity.

When a duplicate command arrives:

```text
COMMAND sequence=N
```

and `N` has already been executed, the receiver sends `ACK(N)` again and does
not trigger the actuator.

This rule handles the common case where:

```text
COMMAND sequence=N arrives
actuator is triggered
ACK sequence=N is lost
COMMAND sequence=N is retransmitted
```

The second command must not produce a second actuator pulse.

With authenticated replay protection enabled, the receiver stores the last
accepted sequence in NVS and applies these rules for the accepted transmitter
identity:

```text
sequence == last_accepted    duplicate, send ACK, do not actuate
sequence >  last_accepted    new command, actuate, record, send ACK
sequence <  last_accepted    replay, drop without ACK
```

The receiver records a new sequence only after the actuator pulse completes. A
failed actuator attempt remains retriable. If the persistent write fails after a
successful pulse, the receiver withholds the ACK and retries the write instead
of returning to normal receive handling. This is intentional: confirming a
command before its accepted sequence is durable would make that signed packet
replayable after a later reset.

Replay resistance depends on intact persistent storage. Erasing TX or RX NVS
state, replacing a board, or cloning firmware and storage images is a
reprovisioning event.

If the persistent counter area is not a valid NVS filesystem, or if an accepted
sequence cannot be recorded, the authenticated applications fail closed. First
provisioning must erase the storage partition before the devices are used.

### What Monotonic Counters Do Not Cover

A counter only rejects sequences the receiver has already passed. A packet
captured off the air that never reached the receiver carries a number still in
the future, so it stays valid until something larger is accepted. An attacker
who jams the receiver while recording can therefore bank a usable command.

The protocol has no defense against this, because the transmitter alone decides
what "fresh" means. Fixing it requires the receiver to contribute freshness — a
challenge-response exchange over the same packet shape — which would change the
message flow and is not part of protocol version 1. `SECURITY.md` states the
consequence for installations.

## Radio Independence

The protocol module owns bytes and validation. It does not own RSSI, SNR, LoRa
configuration, SPI devices, GPIOs, or timing policy.
