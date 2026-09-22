#!/usr/bin/env python3
"""Host acceptance cases for the D7-T1 live bulk exchange."""

import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "d7t1_z_host_sequence", ROOT / "scripts" / "d7t1_z_host_sequence.py"
)
HOST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HOST)


class LiveTransportTests(unittest.TestCase):
    def test_case1_pre_send_prompt_and_post_prompt_pass(self):
        result = HOST.evaluate_live_exchange(True, True, True, True)
        self.assertTrue(result["transport_ok"])
        self.assertEqual(result["HOST_PRE_SEND_PROMPT_OBSERVED"], "yes")
        self.assertEqual(result["LIVE_BIDIRECTIONAL_TRANSPORT_HOST_ONLY"], "yes")
        self.assertEqual(result["FULL_EL0_BIDIRECTIONAL_ACCEPTANCE"], "requires_target_evidence")

    def test_case2_missed_pre_send_prompt_is_not_a_transport_failure(self):
        result = HOST.evaluate_live_exchange(False, True, True, True)
        self.assertTrue(result["transport_ok"])
        self.assertEqual(result["HOST_PRE_SEND_PROMPT_OBSERVED"], "no")
        self.assertEqual(result["HOST_SENT_ABC_LF"], "yes")
        self.assertEqual(result["HOST_POST_READ_PROMPT_OBSERVED"], "yes")
        self.assertEqual(result["LIVE_BIDIRECTIONAL_TRANSPORT_HOST_ONLY"], "yes")
        text = HOST.format_live_exchange(result)
        self.assertNotIn("LIVE_BIDIRECTIONAL_TRANSPORT=no", text)

    def test_case3_missing_post_prompt_fails(self):
        result = HOST.evaluate_live_exchange(True, True, False, True)
        self.assertFalse(result["transport_ok"])
        self.assertEqual(result["HOST_POST_READ_PROMPT_OBSERVED"], "no")
        self.assertEqual(result["LIVE_BIDIRECTIONAL_TRANSPORT_HOST_ONLY"], "no")

    def test_case4_device_absent_fails(self):
        result = HOST.evaluate_live_exchange(False, False, False, False)
        self.assertFalse(result["transport_ok"])
        self.assertEqual(result["LIVE_BIDIRECTIONAL_TRANSPORT_HOST_ONLY"], "no")
        self.assertEqual(result["HOST_SENT_ABC_LF"], "no")


if __name__ == "__main__":
    unittest.main()
