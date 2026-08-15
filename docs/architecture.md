# Architecture

Gate Link is split into two Zephyr applications and shared modules.

```text
apps/transmitter
apps/receiver
common/auth
common/protocol
common/sequence
common/storage
common/radio
```

## Transmitter

The transmitter owns the physical button and the command lifecycle.

Responsibilities:

- read a button through a board-specific devicetree alias;
- debounce the physical signal;
- convert one press into one logical command;
- assign a sequence number;
- persist the assigned sequence before transmission;
- authenticate the command packet;
- send a LoRa command packet;
- wait for a matching ACK;
- authenticate the ACK;
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
- reject packets with invalid authentication tags;
- reject authenticated replay packets older than the last accepted sequence;
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

## Shared Authentication Module

`common/auth` owns the packet authentication tag. It uses mbedTLS
HMAC-SHA256, truncates the result to the fixed 8-byte `auth_tag`, and verifies
tags with a constant-time comparison.

It does not know about LoRa, GPIO, button state, retry timing, or actuator
behavior.

## Shared Sequence Module

`common/sequence` owns receiver-side duplicate and replay decisions. It tracks
the last accepted sequence per transmitter, allows the exact same sequence to be
acknowledged as a duplicate, accepts only higher sequences as new commands, and
rejects older sequences as replay.

Duplicate handling is a reliability rule and replay rejection is a security
rule, but they read the same counter, so they live in one module rather than
two that could disagree about what "already seen" means.

It does not send ACKs, receive radio packets, or drive actuator hardware.

## Shared Counter Storage

`common/storage` owns the NVS-backed monotonic counters used by ESP32 builds.
The transmitter stores the last issued command sequence before sending a packet.
The receiver stores the last accepted sequence after the actuator pulse has
completed. This keeps replay policy out of the radio and actuator code.

## Radio Boundary

`common/radio` is a thin wrapper around the Zephyr radio APIs used by the chosen
LoRa transceiver.

It owns:

- radio initialization;
- send and receive operations;
- radio error normalization;
- radio configuration loading;
- reporting whether this build has a radio at all.

It does not own application retries, command sequencing, ACK validation, or
actuator behavior.

Recovery policy sits outside this boundary too. The wrapper reports failures and
offers a probe that is safe to re-run, while each application decides how many
consecutive failures it tolerates and what it has to reconfigure afterwards
(D012).

## Actuator Boundary

The receiver application calls an actuator interface:

```text
actuator_init()
actuator_trigger()
```

An actuator implementation can pulse a GPIO-backed LED or drive an electrically
isolated output without changing the protocol, radio wrapper, or receiver state
machine.

Every implementation owes the same guarantee at this boundary: the output must
end up inactive. Failing to activate it is an ordinary error that returns to the
caller, which then withholds the ACK so the transmitter retries. Failing to
deactivate it is not, because there is no later step that would fix it, so the
receiver retries until the output is down instead of resuming normal operation
with the gate held open.

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

For the ESP32 DevKitC WROOM bench target, the RFM95W `NSS` signal is the
`spi3` hardware chip select on GPIO5. The overlay intentionally does not declare
`cs-gpios` for this line because the SX1276 register protocol needs chip select
to stay asserted across the address byte and the data byte. If the selected
board or SPI peripheral changes, validate the final chip-select behavior with a
logic analyzer before treating the radio link as reliable.
