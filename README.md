# Gate Link

Gate Link is a point-to-point LoRa remote trigger built with two ESP32 devices
running Zephyr RTOS.

The product goal is intentionally narrow: press one physical button on the
transmitter and produce one short logical pulse on the receiver.

```text
button -> ESP32 TX -> LoRa -> ESP32 RX -> actuator output
```

The receiver output is represented by an actuator interface. During bench
validation this output can be an LED. In an installed system the same interface
can drive an isolated electrical contact, relay driver, optocoupler, transistor,
gate controller input, or remote-control button contact.

The command is named `TRIGGER`, not `OPEN`, because many gate controllers use a
single input to open, stop, or close depending on their current state.

## System Behavior

Target behavior:

```text
TX button press
  -> TX sends COMMAND(TRIGGER, sequence)
  -> RX validates packet
  -> RX ignores duplicate commands already executed
  -> RX triggers the actuator once
  -> RX sends ACK(sequence)
  -> TX reports success only for the matching ACK
```

The system is designed around unreliable wireless communication:

- a command packet can be lost;
- an ACK can be lost;
- a command can be received more than once;
- a button can bounce or remain pressed;
- a stale or unrelated ACK can arrive after another command is already active;
- radio configuration must vary by hardware and region.

## Current Status

The bench firmware has completed Phase 6: structured LoRa packets with ACK,
timeout, retry, and receiver duplicate suppression. Phase 1 actuator/button
GPIO wiring is still pending physical bench wiring.

Implemented now:

- ESP32 DevKitC WROOM-32D boots under Zephyr;
- RFM95W/SX1276 is detected over SPI;
- TX sends binary `COMMAND(TRIGGER, sequence)` packets over LoRa;
- RX decodes and validates the protocol packet;
- RX replies with `ACK(sequence)`;
- TX reports success only for the ACK matching the command in progress;
- TX retransmits the same command sequence after ACK timeout, up to a
  configured retry limit;
- RX suppresses duplicate command execution by accepted transmitter identity and
  still replies with ACK.

Not implemented yet:

- physical button debounce;
- actuator LED pulse;
- cryptographic authentication.

See [docs/status.md](docs/status.md) for the phase checklist.

## Layout

```text
apps/
  transmitter/     Zephyr application for the button-side device
    boards/        Devicetree overlays: button, status LEDs, LoRa wiring
  receiver/        Zephyr application for the actuator-side device
    boards/        Devicetree overlays: actuator output, LoRa wiring
common/
  protocol/        Packet model, encode/decode, and validation
  sequence/        Receiver-side command duplicate suppression
  radio/           Thin wrapper over the Zephyr LoRa APIs
docs/
  architecture.md  System boundaries and component responsibilities
  protocol.md      Packet model, ACK, sequencing, and duplicate handling
  decisions.md     Project decisions that constrain implementation
  status.md        Current implementation phase and bench validation state
tests/
  protocol/        Host tests for packet encoding, validation, and ACK matching
  sequence/        Host tests for duplicate suppression decisions
scripts/
  build_all.sh
```

## Design Constraints

- Communication is LoRa point-to-point, not LoRaWAN.
- Protocol code is independent from the radio driver.
- Receiver application logic calls an actuator abstraction instead of directly
  knowing the final output hardware.
- Hardware pins and board-specific wiring belong in Zephyr devicetree overlays.
- LoRa parameters are configuration, not application constants.
- Sequence numbers are required to match ACKs and prevent duplicate actuator
  pulses after retransmission.
- Security-sensitive fields are part of the packet design so command
  authentication can be added without redesigning the protocol.

See [docs/decisions.md](docs/decisions.md) for the full decision record.

## Build

Prerequisites:

- Zephyr SDK/toolchain installed;
- `west` available in `PATH`;
- a Zephyr workspace initialized with this repository as the manifest
  repository.

The repository does not need to be inside the workspace. A typical local setup
keeps this repository elsewhere and points the workspace `.west/config` at it:

```ini
[manifest]
path = <relative-or-absolute-path-to-gate-link>
file = west.yml

[zephyr]
base = zephyr
```

After the workspace is configured, fetch the modules listed in the manifest:

```sh
cd <zephyr-workspace>
west update
```

For a host build, which defaults to `native_sim/native/64`:

```sh
cd <zephyr-workspace>
<path-to-gate-link>/scripts/build_all.sh
```

To build for the ESP32 bench hardware:

```sh
cd <zephyr-workspace>
BOARD=esp32_devkitc_wroom/esp32/procpu <path-to-gate-link>/scripts/build_all.sh
```

The ESP32 target additionally needs the Espressif binary blobs, once per
workspace:

```sh
west blobs fetch hal_espressif
```

Individual builds:

```sh
west build -p always -b native_sim/native/64 \
  -s <path-to-gate-link>/apps/transmitter \
  -d build/transmitter

west build -p always -b native_sim/native/64 \
  -s <path-to-gate-link>/apps/receiver \
  -d build/receiver
```

Board overlays are named after the board target, so a board is only supported
once `apps/<app>/boards/<board target>.overlay` exists. Building for a target
without an overlay fails at compile time with a message naming the missing
devicetree alias, rather than silently producing firmware with no I/O.

## Tests

The protocol module is independent from radio and GPIO, so it is tested on the
host:

```sh
west twister -T <path-to-gate-link>/tests -p native_sim/native/64
```

## Flash

After selecting the real ESP32 board and creating the required board overlays:

```sh
west flash -d build/transmitter
west flash -d build/receiver
```

## Documentation

- [Architecture](docs/architecture.md)
- [Protocol](docs/protocol.md)
- [Decisions](docs/decisions.md)
- [Status](docs/status.md)

## Buy Me a Coffee

If this project helped you, you can send a few sats over Lightning:

`maquinalab@walletofsatoshi.com`

<img src="assets/lightning-donation-qr.svg" alt="Lightning donation QR code" width="180">

## License

Dual-licensed under [MIT](LICENSE-MIT) or [Apache-2.0](LICENSE-APACHE), at your
option.
