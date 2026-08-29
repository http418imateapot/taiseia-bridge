# ADR 0009 — CMake, with a Makefile only as a convenience wrapper

Status: accepted

## Context

Three build situations: a host build on Linux or macOS, a native build on
Windows, and a cross build for an embedded target. One description should cover
all three, and the cross toolchain must not be written into the build files —
an absolute path to somebody else's toolchain is both a portability failure and
an identifying detail.

## Decision

CMake is the build system. Cross builds go through `CMAKE_TOOLCHAIN_FILE`.
Tests run under CTest.

A thin `Makefile` that shells out to CMake may exist for people whose fingers
type `make`; it must not become a second description of the build.

## Consequences

- A native Windows build needs no POSIX environment. Requiring MSYS2 in order
  to compile a native Windows program would undercut one of the project's
  stated targets.
- CTest gives CI one command across all three platforms, and gives the
  freestanding check somewhere to live that runs by default.
- `compile_commands.json` comes out of the box, so editors and clangd work
  without per-contributor setup.
- CMake is a build-time tool only. Nothing it does is linked into the binary,
  so the no-external-libraries property is unaffected.
- The cost is CMake itself: an awkward language, and one more thing to install.

## Alternatives rejected

**GNU Make.** Fine on POSIX; on Windows it requires an entire POSIX
environment before a native binary can be produced.

**Meson.** Cleaner than CMake, and needs Python, which is a heavier
prerequisite than CMake on the embedded build hosts in view.

**Autotools.** Its Windows story is worse than either.
