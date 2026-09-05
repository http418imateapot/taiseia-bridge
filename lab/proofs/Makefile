# SPDX-License-Identifier: Apache-2.0
# Stable entry points; use the Python runner so tool failures cannot masquerade
# as the intended counterexample. Run only where CBMC and a compiler exist.
PYTHON ?= python3
CBMC ?= cbmc
OUT ?= lab/results/proofs

.PHONY: all verify demo versions clean
all:
	cd ../.. && $(PYTHON) -m lab.proofs.run --cbmc $(CBMC) --output $(OUT)
verify:
	cd ../.. && $(PYTHON) -m lab.proofs.run --suite verify --cbmc $(CBMC) --output $(OUT)
demo:
	cd ../.. && $(PYTHON) -m lab.proofs.run --suite demo --cbmc $(CBMC) --output $(OUT)
versions:
	$(CBMC) --version
clean:
	@echo "Proof results are evidence. Preserve them or choose a new OUT directory."
