# Brief: #1554 — subtract the shaft's cast shadow from the axis support

You are carrying **turnaus#1554** (`gh --repo Apaiko-Oy/turnaus`). Worktree: `C:\Projektit\od-wt-1554`, branch `issue-1554-shadow-support`, cut from main `12b8004`. Read the full issue body first — it holds the acceptance criteria. Commit early and often.

## The measured fact you start from

On rig-20260922's hard-shadow scene, an ACCEPTED shaft axis can sit laterally displaced by up to **192 px** while its angle holds within degrees: the support includes the shaft's own cast shadow, a parallel near-collinear ridge the column-spine fit cannot tell from the shaft. Consequence measured at the solve level by #1512: entry positions 141 mm median off on rig-22 while nearest-tip evidence reads ~5 mm — shadow-displaced lines agreeing with shadow-displaced tips. #1511 therefore shipped the axis as direction-trustworthy/position-suspect. Your job: remove the shadow from the support so the axis becomes a trustworthy position constraint — which is what unblocks the geometric path on the deployment rig.

## Where everything lives

- `src/detector/geometry/detection/shaft_axis.hpp` — the fit, its trim machinery, its gates, and the two findings written in the header for you. Your primary territory.
- Discriminator leads the issue names (measure, do not guess; record refusals with numbers): intensity polarity against the clean reference (a dart is dark-on-board, its shadow is darker-board-on-board — the fresh-diff figure knows which pixels brightened vs darkened), board-plane geometry with the light, width profile. The clean-reference machinery from #1518 is in `dart_processing.*` — read-only for you.
- Annotations: `testers/i1511_annotations/` — 74 lines, the lateral-error census before/after is measured against these. One row is suspect (rig-20260922.csv row `1,3,7,cam3,frame 520` annotates a dart the corrected truth says never hit the board — #1512's report flagged it). **Do not silently use or delete it: verify it against the frame yourself with i1511_frame_tool's check mode when you have Docker, and report what it actually is** (parked dart? shadow? real mismeasure?).
- Prior runs to mine before any container: `C:\Projektit\runs-od-wt-1512\1512\` (census18/22, overlays in probe18geo/probe22geo), `C:\Projektit\od-1511-runs\1511\` (probe18/probe22 overlay JPEGs — the shadow cases are visible in them), plus the committed censuses.

## The bar (from the issue, plus the stack's own standards)

- The discriminator is measured on the fixtures; refused alternatives recorded with numbers.
- On the known 192 px case the accepted axis moves onto the annotated line; lateral-error census before/after over all annotation pairs, both fixtures.
- No coverage collapse: per-camera valid-axis coverage stays within run variance of #1511's census; refusals stay named.
- Mutation proof, prediction first: disabling the subtraction restores the displaced fit on the known case.
- Downstream check: rerun the 1512-entry census — rig-22 position error should fall dramatically (the 141 mm median is mostly this fault); report the number, don't oversell it.
- MSVC: `gh workflow run "Build and release" --repo Apaiko-Oy/OpenDartboard --ref issue-1554-shadow-support`.
- Registry rows: **insert DIRECTLY UNDER the `1511` rows.** Never append at the tail.

## Docker is a shared single resource

**Do not start any container until the supervisor pings "Docker is yours".** #1553's batch holds it right now. Mine the overlays and censuses on disk, design, implement, write testers — then verify in one batch on the ping. Registry (dev) builds calibrate at a 3 s seek window (#1551); all your comparison baselines (runs-od-wt-1512, od-1511-runs) are dev-window and comparable; never compare against `od-baselines/5bc3b0a` rig-22 numbers (release window).

## Boundaries

- A second agent is live on **#1553** in `C:\Projektit\od-wt-1553`, editing `board_model.hpp` and possibly `score_processing.cpp` call sites, plus testers under the `1510p2` anchor. **Do not edit `board_model.hpp` or `score_processing.*`.** Your territory: `shaft_axis.hpp` (+ `entry_intersection.hpp` ONLY if a weighting constant must move — say exactly what you touched), your testers.
- Its branch may merge before yours: before your verification batch, merge origin/main into your branch if it moved.
- The board is unlit: never open live cameras. Never modify `mocks/`. Never judge against `mocks/cam_*.mp4` (#1478). Never push main, never `git stash`, never kill by pattern, no credentials.
- Commits: `#1554: <sentence>`; end with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

## Report

Branch pushed, the discriminator with its measurement, the lateral-error census before/after, coverage before/after, the 1512-entry effect on rig-22, the suspect-annotation verdict, mutation proof verbatim, MSVC run id, refused designs with numbers, anything out of scope (report, don't file).
