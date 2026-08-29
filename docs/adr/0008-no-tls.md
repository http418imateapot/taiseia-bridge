# ADR 0008 — No TLS, and enabling it is refused rather than downgraded

Status: accepted

## Context

TLS cannot be hand-written the way MQTT can; it needs a library, and a library
is the dependency this project otherwise avoids. Meanwhile the deployments in
view use a plaintext broker on the local network.

The dangerous middle ground is a configuration that accepts `tls_enable = true`
and quietly connects in cleartext anyway. An operator who set that key believes
the link is encrypted, and nothing tells them otherwise.

## Decision

No TLS transport. The configuration keys are parsed and validated, and
`tls_enable = true` causes the program to **refuse to start**, with a message
saying no TLS backend is built in.

## Consequences

- Credentials and control commands travel in the clear. This belongs in the
  README as a deployment constraint, not in a footnote.
- An operator cannot be wrong about whether encryption is on: either the
  program runs without it, or it does not run.
- Keeping the keys parsed means a build with a backend needs no configuration
  migration.

## Alternatives rejected

**Silently fall back to cleartext.** The failure mode this decision exists to
prevent.

**Link mbedTLS now.** It ships in many embedded distributions and is far
smaller than OpenSSL, so this is the likely answer eventually. It is not the
answer while there is no southbound implementation to secure and no deployment
asking for it — and it is a decision worth making deliberately rather than as a
side effect of adding a configuration key.
