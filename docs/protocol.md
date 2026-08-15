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

`auth_tag` is part of the packet model so command authentication can be added
without changing the packet shape. Unauthenticated builds send it as zeros and
must still validate all other fields strictly.

Two values are reserved and never valid on the wire:

```text
device_id == 0    device identity not assigned
sequence  == 0    no command in progress
```

Reserving them lets an uninitialized packet be rejected as invalid instead of
travelling, and gives the receiver a distinct "nothing executed yet" state for
duplicate detection.

A frame is accepted only when the length is exactly 19 bytes, the version
matches, the message type and command are known values, and neither reserved
value appears. A frame failing any check is dropped without touching decoded
output, so a caller can never act on a partially parsed packet.

## Sequence Numbers

Each command has a sequence number assigned by the transmitter.

The sequence number is used to:

- match an ACK to the command currently waiting for confirmation;
- distinguish a new command from a retransmission;
- prevent duplicate actuator pulses when an ACK is lost.

## ACK Rules

The receiver sends `ACK(sequence)` after accepting a valid command packet.

The transmitter reports success only when:

```text
message_type == ACK
sequence == current_command_sequence
device_id is accepted
packet validation succeeds
```

ACKs for old, unknown, or unrelated sequences are ignored.

In protocol version 1, the ACK echoes the transmitter `device_id`, `sequence`,
and `command` from the command being acknowledged. This keeps ACK matching tied
to the command currently in progress without adding a receiver identity field
to the initial packet shape.

## Duplicate Command Rules

Duplicate suppression is required behavior, but it belongs to Phase 6 of the
implementation.

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

## Radio Independence

The protocol module owns bytes and validation. It does not own RSSI, SNR, LoRa
configuration, SPI devices, GPIOs, or timing policy.
