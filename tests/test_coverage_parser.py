"""Protect report accuracy: gcov partial lines must not imply full branches."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from run_host_coverage import parse_gcov


class CoverageParserTests(unittest.TestCase):
    def test_unexecuted_lines_and_branch_outcomes(self):
        result = parse_gcov("""        -:    0:Source:sample.c
        3:    1:int f(void) {
       2*:    2:  if (a || b) return 1;
branch 0 taken 0 (fallthrough)
branch 1 taken 2
branch 2 never executed
branch 3 never executed
    #####:    3:  return 0;
    =====:    4:  return -1;
        -:    5:}
""")
        self.assertEqual(result["lines"], 4)
        self.assertEqual(result["lines_hit"], 2)
        self.assertEqual(result["branches_hit"], 1)
        self.assertEqual(result["branches"], 4)
        self.assertEqual(result["uncovered_lines"], [3, 4])
        self.assertEqual(result["uncovered_branches"][0], {"line": 2, "branch": 0})

    def test_missing_instrumentation_is_not_a_pass(self):
        with self.assertRaises(RuntimeError):
            parse_gcov("        -:    0:Source:sample.c\n")


if __name__ == "__main__":
    unittest.main()
