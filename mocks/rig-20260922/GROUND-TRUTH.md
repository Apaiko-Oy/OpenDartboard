8 16 miss, 12 t9 t8, 20 d20 1, 7 t15 4, 4 19 7, 19 18 6, 15 3 t9, t1 4 50

**Visit 1's third dart was recorded as a 7 until 2026-09-24 and is a miss.** The
maintainer corrected it while reading a census that credited the detector with missing
a dart that was never on the board. Recorded rather than quietly fixed, the way
`rig-20260918/GROUND-TRUTH.md` records its own transposition: the error was the
transcription's, and it cost a real conclusion — every count of "darts undetected" made
against the old line was one too high.

**The whole of visit 1 is thrown before the recording's clean frame (~9 s, index ~270).**
The first dart, the 8, is the parked dart #1514 found: it is on the board from frame
one, so it cannot "arrive" and no arrival-based detector can ever score it. What visit 1
offers a detector is therefore one arrival (the 16) and one miss (nothing to see). A
comparison that counts the 8 or the miss as detection failures is measuring the
recording, not the detector.

Derived copies of this line must carry the correction:
`testers/i1499_truth_rig20260922.md` is the table the census harnesses actually parse.
