# Security Policy

Gate Link triggers a physical actuator over an unlicensed radio link. The
current implementation authenticates packets, but it still needs careful key
provisioning before installation.

`COMMAND` and `ACK` packets carry an 8-byte tag computed as HMAC-SHA256 over
the packet fields before `auth_tag`. The shared key is 256 bits and is supplied
through local, unversioned Zephyr configuration:

```conf
CONFIG_GATE_AUTH_KEY_HEX="<64 hex characters>"
```

The repository default is empty. A firmware build that has a radio and no valid
key fails to compile, so an unprovisioned image cannot be flashed by mistake.

## What the Current Design Does Protect Against

- Lost commands and lost ACKs, through retry.
- Duplicate execution, through sequence tracking on the receiver — a repeated
  frame cannot actuate twice.
- Stale or unrelated ACKs arriving after another command is already active.
- Forged commands and forged ACKs that do not have a valid HMAC tag.
- Reuse of older authenticated packets after a newer sequence has been accepted,
  through persisted monotonic counters.

## What the Current Design Does Not Protect Against

**A captured packet that never reached the receiver stays valid.** The receiver
accepts any authenticated sequence greater than the last one it accepted, so a
command that was recorded off the air while the receiver could not hear it
remains usable later. The practical attack is jam-and-replay, the same one that
defeats garage rolling codes:

```text
attacker jams the receiver and records COMMAND seq=5   receiver stays at last=4
user presses again; attacker records seq=6 and
  replays seq=5                                        gate opens, user sees nothing wrong
attacker holds seq=6                                   still valid whenever they choose
```

Authentication does not close this, and neither do monotonic counters: the
captured packet is genuine and its number is still in the future. Closing it
requires the receiver to contribute freshness — a challenge-response exchange,
where the transmitter signs a nonce the receiver just issued. That is not
implemented.

Also outside the current design:

- **Confidentiality.** The LoRa payload is not encrypted. Observers can see
  packet timing, message type, device id, sequence, command, and the tag.
- **Jamming.** An attacker who only blocks the link prevents the gate from being
  operated by radio. Availability is not a property this design provides.
- **Physical extraction.** The key lives in firmware. Anyone who can read flash
  off a board can clone a transmitter; readout protection is a board-level
  concern this project does not configure.

## Deployment Guidance

Do not commit real keys. Use a generated key per installation, and treat any
firmware image containing that key as sensitive.

Replay resistance depends on the TX and RX NVS counters. Erasing flash storage,
replacing a board, or cloning firmware and storage images changes that trust
state and should be treated as reprovisioning.

If the counter storage is missing, invalid, unreadable, or cannot record an
accepted sequence, the firmware fails closed instead of confirming commands
that are not replay-resistant across reset.

Because of the jam-and-replay limitation above, do not rely on Gate Link as the
only barrier where unauthorized actuation matters. Gate controller safety and
access mechanisms must remain in place. Gate Link only provides a remote
trigger; it does not replace obstruction sensing, motor control safety, or
physical access policy.

## Reporting

Report security-sensitive issues privately before public disclosure, through
GitHub's private vulnerability reporting on this repository:

<https://github.com/VinicKMx/gate-link/security/advisories/new>

Please do not open a public issue containing exploit details.
