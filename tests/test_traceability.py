import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from check_traceability import validate_matrix  # noqa: E402


class TraceabilityTests(unittest.TestCase):
    def test_matrix_links_and_status_contract(self):
        counts = validate_matrix()
        self.assertEqual(counts["total"], 14)
        self.assertEqual(counts["verified"], 9)
        self.assertEqual(counts["partial"], 4)
        self.assertEqual(counts["open"], 1)


if __name__ == "__main__":
    unittest.main()
