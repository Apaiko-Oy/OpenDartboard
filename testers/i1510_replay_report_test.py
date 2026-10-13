import unittest
import hashlib
import tempfile
from pathlib import Path
from i1510_replay_report import read_events, summarize, verify_sources


class ReportTests(unittest.TestCase):
    def setUp(self):
        self.truth = {"fixture": "synthetic", "first_complete_visit": 2,
                      "visits": [["S8"], ["S12", "T9", "T8"], ["S20", "D20", "S1"]],
                      "visit_windows_ms": [[9000, 24000], [24000, 36000]]}

    def test_complete(self):
        events = [{"time_ms": t, "score": score} for t, score in
                  [(13000, "S12"), (16000, "T9"), (19000, "S8"),
                   (27000, "S20"), (30000, "D20"), (33000, "S1")]]
        result = summarize(self.truth, events)
        self.assertEqual(result["correct_total"], 5)
        self.assertEqual(result["expected_darts"], 6)

    def test_missing_dart_does_not_shift_next_visit_or_guess_identity(self):
        events = [{"time_ms": t, "score": score} for t, score in
                  [(13000, "S12"), (19000, "T8"),
                   (27000, "S20"), (30000, "D20"), (33000, "S1")]]
        result = summarize(self.truth, events)
        self.assertIsNone(result["correct_total"])
        self.assertEqual(result["unaligned_visits"], [2])
        self.assertEqual(result["visits"][1]["correct"], 3)

    def test_extra_event_is_not_discarded(self):
        result = summarize(self.truth, [{"time_ms": 5000, "score": "T20"}])
        self.assertEqual(len(result["outside_coverage"]), 1)
        self.assertIsNone(result["correct_total"])

    def test_truncation_is_not_full_replay(self):
        with self.assertRaises(ValueError):
            read_events("CAPSEAM iter=1 pos_ms=[10000,10000,10000]\nSCORE: T9 | Position: (1,2)")

    def test_missing_camera_clock_is_not_guessed(self):
        with self.assertRaises(ValueError):
            read_events("SCORE: T9 | Position: (1,2)\nEND OF FOOTAGE: every file source has reached its end")

    def test_camera_time_and_end(self):
        events = read_events("CAPSEAM iter=1 pos_ms=[10000,9800,9600]\n"
                             "SCORE: END | Position: (1,2)\nSCORE: BULL | Position: (1,2)\n"
                             "END OF FOOTAGE: every file source has reached its end")
        self.assertEqual(events, [{"time_ms": 10000, "score": "BULL"}])

    def test_wrong_recording_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "cam_1.mp4"
            path.write_bytes(b"latest recording")
            truth = {"cameras": [{"file": path.name,
                      "sha256": hashlib.sha256(b"latest recording").hexdigest()}]}
            verify_sources(truth, directory)
            path.write_bytes(b"older recording")
            with self.assertRaises(ValueError):
                verify_sources(truth, directory)

    def test_source_time_is_finite_and_monotonic(self):
        for clock in ("nan", "inf", "-1", "9000"):
            with self.subTest(clock=clock), self.assertRaises(ValueError):
                read_events("CAPSEAM iter=1 pos_ms=[10000,10000,10000]\n"
                            f"CAPSEAM iter=2 pos_ms=[{clock},10000,10000]\n"
                            "END OF FOOTAGE: every file source has reached its end")

    def test_appended_replay_is_refused(self):
        with self.assertRaises(ValueError):
            read_events("CAPSEAM iter=1 pos_ms=[10000,10000,10000]\n"
                        "END OF FOOTAGE: every file source has reached its end\n"
                        "SCORE: S1 | Position: (1,2)")

    def test_duplicate_is_not_matched_to_next_dart(self):
        self.truth["throw_dividers_ms"] = [[15000, 18000], [28000, 31000]]
        events = [{"time_ms": t, "score": score} for t, score in
                  [(13000, "S12"), (16000, "T9"), (16500, "T8"),
                   (27000, "S6"), (27500, "S20"), (29000, "D20"), (32000, "S1")]]
        result = summarize(self.truth, events)
        self.assertEqual(result["correct_total"], 4)
        self.assertEqual(result["clean_correct_darts"], 3)
        self.assertEqual(result["missed_darts"], 1)
        self.assertEqual(result["wrong_first_scores"], 1)
        self.assertEqual(result["extra_events"], 2)
        self.assertIsNone(result["visits"][0]["darts"][2]["first_score"])

    def test_invalid_throw_dividers_are_refused(self):
        self.truth["throw_dividers_ms"] = [[18000, 15000], [28000, 31000]]
        with self.assertRaises(ValueError):
            summarize(self.truth, [])


if __name__ == "__main__":
    unittest.main()
