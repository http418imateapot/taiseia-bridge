# CLAUDE.md

The instructions for this repository live in [AGENTS.md](AGENTS.md), which is
the ecosystem-standard filename and is kept as the single copy so the two
cannot drift apart.

Read it before making any change here. The three things most likely to be got
wrong, in order:

1. **The layering rule.** `src/core` and `src/ports` are freestanding; a
   platform dependency added there fails the `core_is_freestanding` test.
2. **The licensed-material boundary.** Clause numbers may be cited; clause text
   and the Annex A service tables may not be reproduced.
3. **Verification honesty.** There is no compiler on this machine and no
   physical appliance. CI is the build environment, the simulator is the test
   fixture, and neither is a substitute for the other.
