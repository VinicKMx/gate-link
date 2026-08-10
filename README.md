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

Expected behavior:

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

## Layout

```text
apps/
  transmitter/     Zephyr application for the button-side device
  receiver/        Zephyr application for the actuator-side device
common/
  protocol/        Packet names, versions, and encode/decode code
  radio/           Thin wrapper over Zephyr LoRa APIs
  utils/           Shared small helpers, if needed
docs/
  architecture.md  System boundaries and component responsibilities
  protocol.md      Packet model, ACK, sequencing, and duplicate handling
  decisions.md     Project decisions that constrain implementation
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
- Zephyr dependencies fetched for the selected manifest.

For a host build, use `native_sim`:

```sh
sh scripts/build_all.sh
```

To build for a specific ESP32 board after choosing the board name:

```sh
BOARD=esp32_devkitc_wroom/esp32/procpu sh scripts/build_all.sh
```

If the exact ESP32 target differs in your Zephyr version, list available boards:

```sh
west boards | rg esp32
```

Individual builds:

```sh
west build -p always -b native_sim apps/transmitter -d build/transmitter
west build -p always -b native_sim apps/receiver -d build/receiver
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
