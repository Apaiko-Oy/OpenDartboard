# The negative corpus both signature arms answer to (#1531)

One directory of manifests. Every one of them must be **refused**, except the one that must
be **accepted**, and every signature arm `src/update/manifest.hpp` has is asked about all of
them and must give the pinned verdict.

- `anchor.hex` — the corpus's own trust anchor, 128 hex characters
- `cases.tsv` — `name`, `expect`, `anchor`, `why`. The `anchor` column is `-` for the
  corpus anchor, or 128 hex characters of its own where the case is about the anchor.
- `<name>.json` — one manifest envelope per case

## Why it is committed rather than minted on every run

`testers/i1305_fixtures.py` mints its fixtures per run, and for what it does that is right:
ECDSA signing is randomised, so a fixture's bytes are not stable and pinning them would be
pinning nothing.

This corpus is the opposite case, because **it is read by two arms that never meet**. The
hand-written `p256_verify.hpp` answers it in the Linux container, on a gate; CNG's
`BCryptVerifySignature` answers it on a Windows runner, in another job, on another machine,
hours apart. If each side minted its own, the two arms would be asked about *different
bytes*, and "these two arms agree" would be a claim about two mintings rather than about two
verifiers. One committed corpus is what makes the comparison mean anything.

The comparison itself needs no channel between the jobs. `cases.tsv` holds the verdict each
case must get, both arms are held to that same column, and so an arm that answers differently
is red **by name, on its own machine** — and two arms cannot disagree about a case without
at least one of them differing from the pin.

The pin is not a third opinion, either. It is what both arms answer today, checked in, so a
change to either arm that moves a verdict has to move this table in the same commit, where a
reader meets it.

## The key

Minted by `testers/i1531_corpus.py` with a **throwaway** P-256 key pair, whose private half
is deleted before that script exits. It is never the release key, it signs nothing anybody
will ever install, and `anchor.hex` is its public half.

That is also what lets the Windows job run this corpus on an **ordinary pull request**: the
corpus carries its own anchor, so the check needs no `ODPRIVATE`, no
`OD_UPDATE_ANCHOR_CURRENT`, and no secret of any kind.

## Re-minting

    testers/i1531_corpus.py

Re-mint when a case is **added**, or when a case's shape changes. Not to refresh it: nothing
in here goes stale, because the curve does not move and neither does DER. A re-mint rewrites
every byte in this directory and produces a diff nobody can read.

## Who reads it

| reader | arm | when |
| --- | --- | --- |
| `testers/i1531_run.sh` (label `1531-corpus`) | `src/update/p256_verify.hpp` | every `run_all.sh` |
| `.github/workflows/release.yml`, step *"Both signature arms answer to one corpus"* | CNG `BCryptVerifySignature` | every pull request, dispatch and tag |

Both compile the same program, `testers/i1531_corpus_check.cpp`, which takes this directory
as its one argument.

**#1532's install-A-then-update-to-B check does not take its manifests from here, and it
could not.** A manifest signs the sha256 and the size of the zip it offers, and that zip holds
a program compiled on the runner, whose bytes move with every toolset update — so a committed
`accept` row would mean committing a binary as well, and this corpus's private key was
destroyed by design, so no row can be re-signed. `testers/i1532_mint.py` mints a fresh
throwaway key per run instead, reusing `keypair` and `sign` from `i1531_corpus.py` rather
than writing them twice. What stays true is the other half: anything that needs manifests a
verifier must *refuse*, and that do not depend on a build's bytes, belongs here.
