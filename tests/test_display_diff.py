#!/usr/bin/env python3
"""
Unit Tests for MSM8996 Display Register State Comparison Tool
"""

import sys
import unittest
from pathlib import Path

# Add project root to sys.path
sys.path.insert(0, str(Path(__file__).parent.parent))
from scripts.display.compare_display_state import compare_snapshots, DiffResult
from scripts.display.msm8996_display_regs import DISPLAY_REGISTERS, Subsystem

class TestDisplayDiff(unittest.TestCase):
    def test_exact_match(self):
        """Test exact match on deterministic registers."""
        golden = {
            0x00994000: 0x10040001,  # DSI_HW_VERSION
            0x009940f0: 0x00000001,  # DSI_CTRL
            0x009941f4: 0x03000104,  # DSI_LANE_CTRL
        }
        actual = {
            0x00994000: 0x10040001,
            0x009940f0: 0x00000001,
            0x009941f4: 0x03000104,
        }
        report = compare_snapshots(golden, actual)
        self.assertEqual(report["verdict"], "PASS")
        self.assertEqual(report["summary"]["MATCH"], 3)
        self.assertEqual(report["summary"]["DIFF"], 0)
        self.assertEqual(report["summary"]["MISSING"], 0)

    def test_masked_match(self):
        """Test that registers match when masked bits match despite irrelevant bit noise."""
        # DSI_CTRL mask is 0x1. Upper bits in actual should be ignored.
        golden = {0x009940f0: 0x00000001}
        actual = {0x009940f0: 0x12340001}  # bit 0 matches, upper bits differ
        report = compare_snapshots(golden, actual)
        self.assertEqual(report["verdict"], "PASS")
        self.assertEqual(report["summary"]["MATCH"], 1)
        self.assertEqual(report["summary"]["DIFF"], 0)

    def test_masked_mismatch(self):
        """Test that difference on masked bit triggers a DIFF failure."""
        golden = {0x009940f0: 0x00000001}
        actual = {0x009940f0: 0x00000000}  # bit 0 differs
        report = compare_snapshots(golden, actual)
        self.assertEqual(report["verdict"], "FAIL")
        self.assertEqual(report["summary"]["DIFF"], 1)
        self.assertEqual(report["summary"]["MATCH"], 0)

    def test_missing_register(self):
        """Test that missing register in actual snapshot triggers MISSING failure."""
        golden = {
            0x00994000: 0x10040001,
            0x009941f4: 0x03000104,
        }
        actual = {
            0x00994000: 0x10040001,
        }
        report = compare_snapshots(golden, actual)
        self.assertEqual(report["verdict"], "FAIL")
        self.assertEqual(report["summary"]["MISSING"], 1)
        self.assertEqual(report["summary"]["MATCH"], 1)

    def test_volatile_skipped(self):
        """Test that volatile registers are skipped and do not fail the comparison."""
        # 0x00994014 (DSI_FIFO_STATUS) is volatile
        golden = {
            0x00994000: 0x10040001,
            0x00994014: 0x00000000,
        }
        actual = {
            0x00994000: 0x10040001,
            0x00994014: 0x31211101,  # different value from live FIFO
        }
        report = compare_snapshots(golden, actual, include_volatile=False)
        self.assertEqual(report["verdict"], "PASS")
        self.assertEqual(report["summary"]["MATCH"], 1)
        self.assertEqual(report["summary"]["VOLATILE_SKIPPED"], 1)
        self.assertEqual(report["summary"]["DIFF"], 0)

    def test_include_volatile_flag(self):
        """Test that volatile registers are evaluated when requested."""
        golden = {0x00994014: 0x00000000}
        actual = {0x00994014: 0x31211101}
        report = compare_snapshots(golden, actual, include_volatile=True)
        self.assertEqual(report["verdict"], "FAIL")
        self.assertEqual(report["summary"]["DIFF"], 1)
        self.assertEqual(report["summary"]["VOLATILE_SKIPPED"], 0)

    def test_subsystem_breakdown(self):
        """Test that subsystem counts are accurately aggregated."""
        golden = {
            0x00994000: 0x10040001,  # DSI_CTRL
            0x0099444c: 0x00000001,  # DSI_PHY
            0x0099486c: 0x0000005d,  # DSI_PLL
            0x008c233c: 0x00000001,  # MMCC
        }
        actual = {
            0x00994000: 0x10040001,
            0x0099444c: 0x00000001,
            0x0099486c: 0x0000005d,
            0x008c233c: 0x00000001,
        }
        report = compare_snapshots(golden, actual)
        self.assertEqual(report["verdict"], "PASS")
        self.assertEqual(report["subsystem_summary"][Subsystem.DSI_CTRL]["MATCH"], 1)
        self.assertEqual(report["subsystem_summary"][Subsystem.DSI_PHY]["MATCH"], 1)
        self.assertEqual(report["subsystem_summary"][Subsystem.DSI_PLL]["MATCH"], 1)
        self.assertEqual(report["subsystem_summary"][Subsystem.MMCC]["MATCH"], 1)

if __name__ == "__main__":
    unittest.main()
