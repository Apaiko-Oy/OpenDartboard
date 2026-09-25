8 16 miss, 12 t9 t8, 20 d20 1, 7 t15 4, 4 19 7, 19 18 6, 15 3 t9, t1 4 50

**Visit 1's third dart was recorded as a 7 until 2026-09-24 and is a miss.** The
maintainer corrected it while reading a census that credited the detector with missing
a dart that was never on the board. Recorded rather than quietly fixed, the way
`rig-20260918/GROUND-TRUTH.md` records its own transposition: the error was the
transcription's, and it cost a real conclusion — every count of "darts undetected" made
against the old line was one too high.

**The whole of visit 1 is thrown before the recording's clean frame (~9 s, index ~270).**
The first dart, the 8, is the parked dart #1514 found: it is on the board from frame
one, so it cannot "arrive" and no arrival-based detector can ever score it. A comparison
that counts the 8 as a detection failure is measuring the recording, not the detector.

**The miss is on the recording, and this file said it was not (corrected 2026-09-25).**
The sentence here read "one miss (nothing to see)". #1585 measured otherwise: the third
dart of visit 1 arrives at frame 123 and rests in the board's outer black ring, off the
scoring area, and all three cameras see it. Both halves are true at once — it scores
nothing, which is why the line above says `miss`, and it is a real arrival a detector may
legitimately see and must publish as `MISS`, the way `rig-20260918`'s visit-4 miss is
treated. It is annotated as an arrival in `testers/i1511_annotations/`. The only throw of
this recording that cannot arrive is the parked 8.

Derived copies of this line must carry the correction:
`testers/i1499_truth_rig20260922.md` is the table the census harnesses actually parse.
