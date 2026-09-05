# R21 step 5 Applying the checked oracle to C

The proof runner is `python -m lab.proofs.run`. It requires CBMC and a C
preprocessor and runs in CI. The code under `src/` is the codec subject;
`lab/subject/metadata.c` is a separate research prototype, not a production port.

## Harnesses

| Entry | Arbitrary inputs and property |
|---|---|
| h_encode | any type code and int32 value, either modeled layout; acceptance and bytes equal the arithmetic reference |
| h_decode | any type code and byte pair, any of the four modeled readings; acceptance and value equal the reference |
| h_roundtrip | carryable type and value in its domain; P1 and P2 |
| h_parse | arbitrary bytes and held length 0–9; accepted grammar, carryability, result vocabulary and unchanged output on failure |
| h_bad_line | arbitrary first row; failure poisons startup and later good rows cannot recover it |
| h_duplicate | duplicate key with differing types; whole configuration refuses startup |
| h_capacity | every slot usable, next distinct declaration fails and prevents startup |
| h_config | arbitrary config count, readiness, entries and query; lookup is bounded and returns an existing matching entry |
| h_registration | arbitrary typed/untyped registration data, count and query; no invented type and port checks carryability |
| h_port | arbitrary adapter result and output byte, including success without writing output; only carryable types pass |
| h_no_metadata | every raw byte pair survives unchanged and ENOTFOUND remains visible |
| h_read | arbitrary type/bytes; complete lab typed read agrees with oracle or returns raw fallback |
| h_write | arbitrary type/int32 value and either layout; complete lab typed write agrees or preserves output |
| h_arguments | null pointers and short buffers are refused without modifying outputs |
| h_naive | expected failures in both naive signed conversions; tool failures cannot satisfy this demonstration |

The eleven metadata entries share one source file and are selected by
`--function`. Config capacity is four rows; a parse object holds nine bytes.
These are explicit research bounds, not general proofs of arbitrary file size.
All registered arrays provide their actual capacity. Callback memory safety is
assumed; arbitrary callback outputs are not assumed valid.

## Checks and evidence

```text
--bounds-check --pointer-check --pointer-overflow-check
--conversion-check --signed-overflow-check --unsigned-overflow-check
--div-by-zero-check --undefined-shift-check
--unwind 32 --unwinding-assertions
```

Bounds must be sufficient for all loops. Unwinding failures remain failures.
All proofs require exit code 0, a successful CBMC status, a nonempty property
set and every property successful. The demonstration requires exit code 10 and
both specific signed-conversion failures, with no unrelated failures.

The public make targets are preserved:

```sh
make -C lab/proofs verify
make -C lab/proofs demo
```

Both targets should return zero when their expectations hold. The underlying
CBMC run in `demo` must fail for the specified reason. Logs are structured JSON,
not a grep of an arbitrary failure message. Full-run evidence is archived with
the source commit; see [reproduce.md](reproduce.md).

## What a successful result means

The solver reasons about its chosen C target model, shared type table, modeled
inputs and bounded objects. It does not prove all C99 implementations, arbitrary
third-party C code, full application behavior, or hardware interoperability.
The signed conversion demonstration concerns implementation-defined behavior
and value preservation; it should not be described as two UB findings.

The Python oracle is checked before C. The C reference is another transcription
of the equations and shares type metadata deliberately. The exhaustive C
experiment also compares the reference across every carryable domain and byte
pair; its counts are compared to the independent Python result.

A1–A4 and remaining production integration work are documented in
[paper-r21.md](paper-r21.md). No physical appliance has been used.
