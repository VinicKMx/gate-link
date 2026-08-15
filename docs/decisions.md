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

## D011 - Use ESP32 SPI Hardware Chip Select for RFM95W

Decision: on the ESP32 DevKitC WROOM bench target, the RFM95W `NSS` line on
GPIO5 is driven by the `spi3` hardware chip select from the board's default
pinctrl. The `lora0` overlay must not declare `cs-gpios` for that same line.

Reason: with Zephyr 3.7.0 on ESP32, declaring `cs-gpios` makes the SPI driver
avoid keeping the hardware chip select active across a multi-buffer transfer.
The logic analyzer showed `NSS` rising between the SX1276 register address byte
and the data byte. The SX1276 requires `NSS` to remain low for the whole
address+data transaction; otherwise it treats each byte as a separate operation
and returns `0x00`, including when reading `REG_VERSION` at `0x42`.

Implementation constraint: ESP32 overlays using the board's
`SPIM3_CSEL_GPIO5` pinctrl must leave chip select under SPI hardware control.
When changing board, SPI bus, or LoRa module wiring, verify with a logic
analyzer that `NSS` stays low for the complete register transaction and that
the SX1276 version register reads `0x12`.

## D012 - Never Park the Firmware on a Radio Failure

Decision: neither application may end up in a state where a radio failure
leaves it permanently idle. A radio that does not answer is retried forever, at
a configured interval, and recovery re-probes the module and reprograms the
modem before normal operation resumes.

Reason: an installed gate trigger is unattended. A transient SPI glitch at
power-on, a brownout, or a briefly disconnected module must not require a human
to power cycle the board. "Dead and silent" is the worst failure mode for this
product, and it is indistinguishable from a working unit until someone presses
the button.

Implementation constraint: a failure to reach the radio must never terminate a
control loop. Only genuine radio errors count toward recovery. A command that
exhausts its retries without an ACK is a link failure and must not trigger
recovery, since the local radio is proven to work by the fact that it
transmitted. Builds without a radio at all are a separate case: they are
detected with `gate_radio_is_present()` and idle deliberately, because no amount
of retrying creates hardware that is not there.

## D013 - Defer the Hardware Watchdog Until the Actuator Failure Policy Exists

Decision: Phase 7 does not enable a hardware watchdog yet.

Reason: the current firmware already bounds command waits, ACK waits, retries,
radio recovery pacing, and actuator pulse duration. Most intentionally
unbounded waits are safe operating states: the transmitter waiting for a button
event, the transmitter waiting for button release, and the receiver waiting for
packets or radio recovery. One is not: the receiver retries forever to
de-energize a stuck actuator output, because that is the one state where
staying busy matters more than staying responsive. Adding a watchdog before defining the installed
actuator's electrical safe state could hide the more important question: what
must happen if the output driver is stuck active or the radio never recovers.

Implementation constraint: when a watchdog is added, it must be fed only from a
health policy that proves the main loop, radio state, and actuator safe state
are sane. It must not be fed blindly from a timer callback.

## D014 - A Missing I/O Device Is a Build Error, Not a Runtime Failure

Decision: when a button, LED, or actuator GPIO cannot be acquired or configured
at startup, the application logs the failure and stops. It does not retry the
way a radio failure is retried (D012).

Reason: the difference is whether retrying can ever succeed. A radio module can
be unpowered, miswired, or briefly wedged, and heal without anyone touching it.
A GPIO controller that is not ready, or a pin that refuses to configure, means
the devicetree overlay does not describe the board that is running. No amount of
retrying produces a pin the build never bound, so retrying would only turn a
deterministic configuration error into a board that looks alive and silently
never works. Each alias is already asserted at build time, so reaching this path
at all means the overlay and the hardware disagree.

Implementation constraint: this covers only acquiring and configuring the pins
at startup. It does not extend to runtime I/O errors, which are recoverable and
are handled as such: a failed button read keeps the transmitter waiting instead
of fabricating a command, and a failed actuator turn-off is retried forever
rather than leaving the output energized.

## D015 - Refresh RX Mode After Receive Timeouts

Decision: the receiver probes the SX1276 and re-applies RX mode after receive
timeouts.

Reason: bench testing showed that removing power from the RFM95W while the
receiver was already running did not necessarily produce a hard `lora_recv()`
error. The driver could keep returning receive timeouts, which normally mean
"no packet heard". If the firmware treats every timeout as proof that the radio
is healthy, a powered-down or reset SX1276 can remain silent forever until the
ESP32 is rebooted.

Implementation constraint: RX timeout handling must stay quiet on the success
path but must verify that the chip still answers and that RX mode is
programmed. If probing fails, the receiver enters the same paced recovery loop
used for other radio failures and only returns to packet handling after the
module answers and RX mode is configured again.
