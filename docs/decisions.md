# Decisions

This document records technical decisions that constrain implementation.

## D001 - Use LoRa Point-to-Point

Decision: communication uses LoRa point-to-point packets between one transmitter
and one receiver.

Reason: the product needs a small direct remote trigger, not a network join
procedure, gateway, cloud service, MQTT broker, or LoRaWAN infrastructure.

Implementation constraint: application code must not assume Wi-Fi, Bluetooth,
MQTT, cloud services, or LoRaWAN.

## D002 - Name the Command `TRIGGER`

Decision: the actuator command is named `TRIGGER`.

Reason: the final electrical contact may be connected to a gate controller input
or to a remote-control button contact. In those systems the same input can open,
stop, or close depending on the current controller state.

Implementation constraint: code, logs, and protocol definitions must not encode
the command as `OPEN`.

## D003 - Keep the Actuator Behind an Interface

Decision: receiver logic calls an actuator interface instead of controlling the
final output hardware directly.

Reason: the bench output can be an LED, while the installed output can be a
relay driver, optocoupler, transistor, isolated contact, or remote-control
button contact.

Implementation constraint: packet handling and receiver state logic must not
change when the output hardware changes.

## D004 - Keep Protocol Independent From Radio

Decision: protocol encode/decode and validation are independent from the LoRa
driver wrapper.

Reason: packet correctness and transport mechanics are separate concerns.

Implementation constraint: `common/protocol` must not include radio driver
headers or call send/receive APIs.

## D005 - Use Devicetree for Hardware Binding

Decision: hardware pins and devices are described with Zephyr devicetree
overlays.

Reason: the selected ESP32 board and LoRa module determine the actual wiring.

Implementation constraint: application logic must not hardcode ESP32 GPIO
numbers for buttons, LEDs, SPI chip select, radio reset, or radio IRQ lines.

## D006 - Treat LoRa Parameters as Configuration

Decision: LoRa frequency, bandwidth, spreading factor, coding rate, TX power,
and preamble length are configuration values.

Reason: the valid frequency and reliable radio profile depend on the module,
antenna, region, and installation environment.

Implementation constraint: radio parameters must not be scattered as magic
numbers in application state machines.

## D007 - Require ACK Matching

Decision: the transmitter accepts only an ACK matching the current command
sequence.

Reason: stale ACKs and unrelated packets must not falsely indicate success.

Implementation constraint: timeout, retry, and success handling must compare the
received ACK sequence against the command currently in progress.

## D008 - Deduplicate Receiver Execution

Decision: the receiver acknowledges duplicate valid commands but does not
execute the actuator more than once for the same accepted sequence.

Reason: an ACK can be lost after the actuator was already triggered, causing the
transmitter to retransmit the same command.

Implementation constraint: duplicate detection is part of receiver correctness,
not an optional diagnostic feature.

## D009 - Reserve Authentication in the Packet Model

Decision: the packet model includes an authentication field.

Reason: command authenticity and replay resistance are required for an installed
gate trigger, even when a bench build runs without cryptographic validation.

Implementation constraint: security must use established cryptographic
primitives. The project must not implement a custom authentication algorithm.

## D010 - Keep Runtime Behavior Deterministic

Decision: runtime paths should avoid unnecessary dynamic allocation and
unbounded waits.

Reason: a remote trigger should fail predictably when radio communication or
hardware initialization fails.

Implementation constraint: button events, retries, ACK waits, and actuator pulse
duration must have explicit limits.
