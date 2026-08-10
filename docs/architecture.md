# Architecture

Gate Link is split into two Zephyr applications and shared modules.

```text
apps/transmitter
apps/receiver
common/protocol
common/radio
```

## Transmitter

The transmitter owns the physical button and the command lifecycle.

Responsibilities:

- read a button through a board-specific devicetree alias;
- debounce the physical signal;
- convert one press into one logical command;
- assign a sequence number;
- send a LoRa command packet;
- wait for a matching ACK;
- retry within configured limits;
- expose status through logs and LEDs.

The transmitter state machine is explicit:

```text
IDLE -> PREPARE_COMMAND -> TRANSMIT -> WAIT_ACK
WAIT_ACK -> SUCCESS
WAIT_ACK -> RETRY -> TRANSMIT
WAIT_ACK -> ERROR
SUCCESS -> IDLE
ERROR -> IDLE
```

## Receiver

The receiver owns packet validation, duplicate detection, and actuator
execution.

Responsibilities:

- receive LoRa packets;
- parse protocol packets;
- reject invalid packets;
- detect commands already executed;
- trigger the actuator once for each new valid command;
- send ACKs for valid commands, including duplicates already executed;
- expose diagnostic events through logs and LEDs.

The receiver flow is explicit:

```text
WAIT_PACKET -> PARSE -> VALIDATE -> CHECK_SEQUENCE -> EXECUTE -> SEND_ACK
CHECK_SEQUENCE -> SEND_ACK
VALIDATE -> DROP
SEND_ACK -> WAIT_PACKET
DROP -> WAIT_PACKET
```

## Shared Protocol Module

`common/protocol` owns packet representation, versioning, message types,
commands, encode/decode, and validation rules.

It does not call Zephyr LoRa APIs and does not know about GPIOs, buttons, LEDs,
or actuator hardware.

## Radio Boundary

`common/radio` is a thin wrapper around the Zephyr radio APIs used by the chosen
LoRa transceiver.

It owns:

- radio initialization;
- send and receive operations;
- radio error normalization;
- radio configuration loading.

It does not own application retries, command sequencing, ACK validation, or
actuator behavior.

## Actuator Boundary

The receiver application calls an actuator interface:

```text
actuator_init()
actuator_trigger()
```

An actuator implementation can pulse a GPIO-backed LED or drive an electrically
isolated output without changing the protocol, radio wrapper, or receiver state
machine.

## Hardware Boundary

Board-specific details belong in Zephyr devicetree overlays:

- button GPIO;
- status LEDs;
- actuator output GPIO;
- SPI bus;
- LoRa chip select;
- LoRa reset;
- LoRa IRQ/DIO/BUSY lines.

Application code should consume devicetree aliases and device handles instead of
hardcoded ESP32 GPIO numbers.
