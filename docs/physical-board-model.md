# Physical board model experiment (#1510)

**Work in progress; disabled by default.** All three selected rig cameras pass the
physical landmark-fit gate, but full physical-mode replay scores **14/19 correctly
(73.7%)**, compared with **15/19 (78.9%)** on the normal path. Visit 2 throw 2 changes
from the correct T14 to S14. The cause of that regression remains under investigation;
this branch is not ready to replace default scoring or close #1510.

## Model and measurements

Each camera has one homography between image pixels and board millimetres: origin
at the bull, +Y through 20, +X through 6, with the normal clockwise number order.
All six scoring circles and twenty radial boundaries derive from a versioned profile.
Default scoring radii are 6.35, 15.9, 99, 107, 162, and 170 mm. The 225 mm outer rim
is metadata, not the scoring radius. The map is not a calibrated 3D camera pose.

Existing calibration and a numbered anchor initialize the search. The fitter uses
colour-band centres for treble/double rings and directly observed black/white radial
boundaries across the single beds. Band centres come from paired local colour
crossings; colour fringes can widen a band without moving its centre. Clipped and
implausibly narrow/wide bands are excluded before fitting. Raw colour edges remain
separate diagnostics: they are not claimed to be measured metal-wire centres.

A robust fit jointly adjusts eight projective parameters. Angular blocks of ring
landmarks and whole radial sectors are withheld from optimization. The report gives
training/held-out residuals, band/edge support separately, coverage and linearized
parameter uncertainty. All scoring circles, including bull circles, are model
predictions. Overlays show those circles and the fit/held-out landmark locations.

Held-out observations share the initial search map and image with training data.
They are not independent human labels. Nearby pixels are correlated; parameter
uncertainty can underestimate error. Lens parameters are unmeasured; the homography
does not correct distortion. Measured residuals characterize the observed landmarks,
not a global lens-error bound or proof that all scoring boundaries are within 2 mm.
Runtime initialization still depends on legacy bull/ring admission and a numbered
anchor; the fitter itself can constrain a three-quadrant partial board without a bull.

## Usage and persistence

Run the usual detector command with `OD_BOARD_MODEL=physical` to enable the experiment.
Omit the variable for normal scoring. Refused cameras cannot score.
`OD_BOARD_PROFILE=/absolute/path/profile.json` selects a configurable profile:

```json
{"profile":{"id":"club-board","version":1,
 "radii_mm":[6.35,15.9,99,107,162,170],
 "numbers_clockwise":[20,1,18,4,13,6,10,15,2,17,3,19,7,16,8,11,14,9,12,5],
 "rim_radius_mm":225,"boundary_tolerance_mm":2}}
```

The 2 mm landmark-fit budget is provisional: one quarter of the nominal 8 mm scoring
bed width, not a manufacturer tolerance or a validated scoring accuracy specification.
Increasing it does not establish accuracy.

Versioned JSON files go to `cache/physical_board/`; overlays go to
`debug_frames/physical_board/`. The legacy raw-byte cache layout is unchanged.
Camera source/slot, capture dimensions/rate, profile, transform and quality are stored.
Unknown metrics are JSON null. The application JSON serializer handles apostrophes,
backslashes and nulls; the older OpenCV JSON writer/reader was unsuitable here.

Loading a matching sidecar never grants scoring permission. Every physical-mode
startup measures current images, even with `--reuse-calibration`. Geometry is sealed
after initialization. Recovery requires current physical landmarks for every scoring
camera, compares the mappings in millimetres and never adopts the new fit. Unreadable
anchors/landmarks cannot certify recovery; a moved or rotated board requires restart.

## Recorded validation

The user selected the last rig clips: `mocks/rig-20260918/cam_{1,2,3}.mp4`.
SHA-256 confirms these match the three camera files in Downloads. No upstream mock
or separate recording was used. Separate September 22/live capture validation has
not been performed; the user directed this work to use these last rig clips.

Physical-mode startup for the full replay measured:

| Camera | Held-out landmark P95, mm | Raw colour-edge P95, mm | Fit accepted |
|---|---:|---:|---|
| 1 | 1.488 | 2.113 | Yes |
| 2 | 1.477 | 2.147 | Yes |
| 3 | 1.980 | 1.987 | Yes |

These columns measure different things. Replacing colour-edge observations with
band centres changes the measurement definition; the smaller residual is not itself
proof of improved wire-boundary accuracy. An independent synthetic colour rendering
with red bands widened on both sides verifies recovery of the true scoring boundaries
within 1 mm. Independent real-image boundary validation remains outstanding.

Both full replays emit 19 scores across seven visits and reach EOF. Comparison uses
the documented fixed missing events (visit 6 throw 1 and visit 7 throw 3); OUTER is not
counted as a correct MISS. Physical mode changes only the T14 event to S14 in the
score sequence, producing the 14/19 result above. It does not improve accuracy yet.

Run `testers/run_all.sh 1510` after building. The gate requires synthetic assertions,
a mutation check that removes held-out acceptance, and acceptance of all three rig
fits. `OD_1510_REQUIRE_ACCEPTED=0` explicitly selects measurement-only mode.
Controls cover independent surface points, image extraction, colour fringes, partial
support, wrong anchors/ring identity, mirroring, distortion, persistence and recovery.
The full repository suite has not been run; older scripts still use prohibited mocks.

## Remaining work

Investigate the T14 regression against the actual entry point and per-camera tips;
validate physical boundaries independently on real images; complete lifecycle
integration coverage; then rerun matched end-to-end accuracy before considering a
default rollout. Shaft measurement (#1511) and common entry-point fusion (#1512) are
separate tracked work. This implementation does not claim to complete those issues.
