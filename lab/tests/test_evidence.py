"""Regression tests for the evidence gates, independent of full expensive runs."""

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from lab.evidence import ROOT, source_hashes
from lab.oracle import agree, smt
from lab.oracle.types import all_types, carryable_types
from lab.paper import check_claims
from lab.proofs.run import verdict


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.enum = json.loads((ROOT / "lab/results/matrix.json").read_text(encoding="utf-8"))
        self.solver = json.loads((ROOT / "lab/results/smt.json").read_text(encoding="utf-8"))
        # Historical fixtures exercise schema checks; they are not fresh runs.
        for doc in (self.enum, self.solver):
            doc["sources"] = source_hashes()

    def test_complete_consistent_evidence_is_accepted(self):
        self.assertEqual(agree.compare(self.enum, self.solver), [])
        self.assertEqual(check_claims(self.enum, self.solver)[1], [])

    def test_missing_exactness_row_is_rejected(self):
        self.solver["exactness"].pop()
        with self.assertRaisesRegex(ValueError, "incomplete"):
            agree.compare(self.enum, self.solver)

    def test_both_empty_matrices_are_not_vacuous_success(self):
        for doc in (self.enum, self.solver):
            doc["cells"] = []
        with self.assertRaisesRegex(ValueError, "incomplete"):
            agree.compare(self.enum, self.solver)

    def test_duplicate_rows_are_not_silently_collapsed(self):
        self.enum["cells"].append(copy.deepcopy(self.enum["cells"][0]))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            agree.compare(self.enum, self.solver)

    def test_stale_evidence_is_rejected_even_when_both_runs_agree(self):
        for doc in (self.enum, self.solver):
            doc["sources"] = {}
        with self.assertRaisesRegex(ValueError, "stale"):
            agree.compare(self.enum, self.solver)

    def test_each_exactness_direction_is_compared_even_if_both_are_inexact(self):
        row = next(r for r in self.solver["exactness"]
                   if r["type"] == "INT8" and r["reading"] == "wide")
        row["over_witness"] = "12 FD"
        self.assertTrue(agree.compare(self.enum, self.solver))

    def test_unsafe_cell_cannot_be_labelled_unsat(self):
        row = next(r for r in self.solver["cells"] if r["misread"])
        row["unsat"] = True
        self.assertTrue(agree.compare(self.enum, self.solver))

    def test_wrong_value_count_is_rejected(self):
        self.solver["cells"][0]["misread"] += 1
        self.assertTrue(agree.compare(self.enum, self.solver))

    def test_unknown_is_never_interpreted_as_unsat(self):
        class Unknown:
            def add(self, *args):
                pass

            def check(self):
                return smt.z3.unknown

            def reason_unknown(self):
                return "test timeout"
        t = carryable_types()[0]
        with patch.object(smt.z3, "Solver", Unknown):
            with self.assertRaisesRegex(RuntimeError, "unknown"):
                smt.misread(t, "A", "wide")
            with self.assertRaisesRegex(RuntimeError, "unknown"):
                smt.not_exact(t, "lenient")

    def test_missing_solver_returns_nonzero_with_actionable_message(self):
        code = ("import sys; from lab.oracle import __main__, sweep; "
                "sweep.main=lambda:0; sys.exit(__main__.main())")
        p = subprocess.run([sys.executable, "-S", "-c", code], cwd=ROOT,
                           capture_output=True, text=True, timeout=10)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("z3 is not installed", p.stdout)

    def test_partial_type_transcription_is_rejected(self):
        source = (ROOT / "src/core/taiseia/datatype.c").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "types.c"
            path.write_text(source.replace('"INT8"', "NOT_A_STRING"), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unparsed"):
                all_types(str(path))

    def test_duplicate_type_codes_are_rejected(self):
        source = (ROOT / "src/core/taiseia/datatype.c").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "types.c"
            path.write_text(source.replace('{ 0x31, "INT16"', '{ 0x30, "INT16"'), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate"):
                all_types(str(path))


class ProofGateTests(unittest.TestCase):
    def test_proof_requires_success_properties_and_success_status(self):
        doc = [{"result": [{"status": "SUCCESS"}]}, {"cProverStatus": "success"}]
        self.assertTrue(verdict(0, doc))
        self.assertFalse(verdict(0, []))
        self.assertFalse(verdict(1, doc))
        doc[0]["result"][0]["status"] = "UNKNOWN"
        self.assertFalse(verdict(0, doc))

    def test_demo_requires_both_specific_conversion_counterexamples(self):
        rows = [{"status": "FAILURE", "description": "arithmetic overflow on signed type conversion",
                 "sourceLocation": {"function": name}} for name in ("naive_sigma8", "naive_sigma16")]
        doc = [{"result": rows}, {"cProverStatus": "failure"}]
        self.assertTrue(verdict(10, doc, demo=True))
        self.assertFalse(verdict(6, doc, demo=True))
        self.assertFalse(verdict(127, [], demo=True))
        rows.pop()
        self.assertFalse(verdict(10, doc, demo=True))

    def test_unrelated_counterexample_does_not_validate_demo(self):
        doc = [{"result": [{"status": "FAILURE", "description": "pointer dereference",
                            "sourceLocation": {"function": "main"}}]},
               {"cProverStatus": "failure"}]
        self.assertFalse(verdict(10, doc, demo=True))


class InstallerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("lab_install", ROOT / "scripts/install.py")
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)

    def test_selection_is_explicit_and_bounded(self):
        items = [{"path": "first"}, {"path": "second"}]
        self.assertEqual(self.module.choose(items, "2"), items[1])
        for value in ("", "0", "3", "-1", "first"):
            with self.assertRaises(ValueError):
                self.module.choose(items, value)

    def test_probe_refuses_virtual_environment_and_old_python(self):
        for version, is_venv in (([3, 8, 9], False), ([3, 14, 2], True)):
            response = subprocess.CompletedProcess([], 0, json.dumps(["python", version, is_venv]))
            with patch.object(self.module.subprocess, "run", return_value=response):
                self.assertIsNone(self.module.probe("python"))

    def test_probe_handles_missing_interpreter_without_success(self):
        with patch.object(self.module.subprocess, "run", side_effect=FileNotFoundError):
            self.assertIsNone(self.module.probe("missing"))


if __name__ == "__main__":
    unittest.main()
