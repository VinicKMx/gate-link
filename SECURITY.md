# Security Policy

Gate Link triggers a physical actuator over an unlicensed radio link. The
security-relevant fact about the current implementation is short:

**Commands are not authenticated.** Any transmitter configured with the same
radio parameters can trigger the receiver.

The packet model reserves an `auth_tag` field so authentication can be added
without changing the packet shape, and [D009](docs/decisions.md) records that
decision. Until that field carries a real tag, treat the link as an open remote
control.

## What the Current Design Does Protect Against

- Lost commands and lost ACKs, through retry.
- Duplicate execution, through sequence tracking on the receiver — a repeated
  frame cannot actuate twice.
- Stale or unrelated ACKs arriving after another command is already active.

Those are reliability properties. None of them stop a deliberate attacker.

## Deployment Guidance

Do not use Gate Link as the only barrier on an entrance where unauthorized
actuation matters. The gate controller's own safety and access mechanisms must
remain in place.

When authentication is implemented, it must use established cryptographic
primitives. The project will not ship a custom authentication algorithm.

## Reporting

Report security-sensitive issues privately before public disclosure. Until a
dedicated security contact exists, open a minimal issue asking for a private
contact path, without publishing exploit details.
