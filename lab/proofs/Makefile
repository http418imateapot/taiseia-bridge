# Copyright 2026 Calvin
# SPDX-License-Identifier: Apache-2.0
#
# Bounded model checking of the general-format codec.
#
#   make verify   the implementation equals the equations, for every input
#   make demo     ...and CBMC rejects the obvious way of writing it
#   make versions what produced the numbers
#
# There is no compiler on the primary development machine, so this is run by
# CI. Nothing here is claimed to have passed without a run in lab/results/proofs
# to point at.

CBMC ?= cbmc

ROOT := ../..
SRC  := $(ROOT)/src
OUT  := $(ROOT)/lab/results/proofs

INCS := -I$(SRC) -I.

# The implementation under test, and the table it and the reference share.
CORE := $(SRC)/core/taiseia/general.c $(SRC)/core/taiseia/datatype.c
REF  := reference.c

# Every check that can bear on this code. --conversion-check is the one that
# matters most here: it is what separates "the compiler did what we wanted"
# from "the source says what we meant".
CHECKS := --bounds-check \
          --pointer-check \
          --pointer-overflow-check \
          --conversion-check \
          --signed-overflow-check \
          --unsigned-overflow-check \
          --div-by-zero-check \
          --undefined-shift-check

# ts_datatype() scans a 25-row table, so there is one loop and it is bounded by
# the table. --unwinding-assertions is not optional: without it an insufficient
# bound silently proves a smaller program than the one you have, which is the
# exact shape of vacuity this whole directory exists to catch. If the table
# grows past this bound the proof fails loudly.
BOUND := --unwind 32 --unwinding-assertions

HARNESSES := h_encode h_decode h_roundtrip

.PHONY: all verify demo versions clean

all: verify demo

$(OUT):
	@mkdir -p $(OUT)

versions:
	@$(CBMC) --version | sed 's/^/cbmc /'

verify: | $(OUT)
	@echo "cbmc $$($(CBMC) --version)" | tee $(OUT)/versions.txt
	@fail=0; \
	for h in $(HARNESSES); do \
	    echo ""; \
	    echo "=== $$h"; \
	    if $(CBMC) $$h.c $(REF) $(CORE) $(INCS) $(CHECKS) $(BOUND) \
	           > $(OUT)/$$h.log 2>&1; then \
	        :; \
	    else \
	        fail=1; \
	    fi; \
	    grep -E "^\*\* [0-9]+ of [0-9]+ failed" $(OUT)/$$h.log || true; \
	    grep -E "Runtime decision procedure" $(OUT)/$$h.log || true; \
	    grep -E "^VERIFICATION" $(OUT)/$$h.log || true; \
	done; \
	if [ $$fail -ne 0 ]; then \
	    echo ""; \
	    echo "a proof failed; the logs are in $(OUT)"; \
	fi; \
	exit $$fail

# Expected to fail. A clean run here is the error, because the point is that
# CBMC rejects what every test in this repository accepts.
demo: | $(OUT)
	@echo "=== h_naive (expected to be REJECTED)"
	@if $(CBMC) h_naive.c naive.c $(INCS) $(CHECKS) $(BOUND) \
	       > $(OUT)/h_naive.log 2>&1; then \
	    echo "UNEXPECTED: the obvious version verified clean."; \
	    echo "Either these flags no longer check conversions or the harness"; \
	    echo "drifted. Do not cite the demonstration until this fails again."; \
	    exit 1; \
	else \
	    echo "rejected, as intended:"; \
	    grep -E "^\*\* [0-9]+ of [0-9]+ failed" $(OUT)/h_naive.log || true; \
	    grep -iE "conversion" $(OUT)/h_naive.log | head -8 || true; \
	fi

clean:
	@rm -rf $(OUT)
