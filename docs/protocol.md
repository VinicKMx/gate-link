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

Protocol packets reserve these fields:

```text
protocol_version
message_type
device_id
sequence
command
auth_tag
```

`auth_tag` is part of the packet model so command authentication can be added
without changing the packet shape. Unauthenticated builds must still validate
all other fields strictly.

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

## Duplicate Command Rules

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
