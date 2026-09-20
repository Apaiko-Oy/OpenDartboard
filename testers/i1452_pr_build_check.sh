#!/bin/bash
# #1452: the Windows build runs on every pull request, and this is what keeps it running.
#
#   bash testers/i1452_pr_build_check.sh [path/to/release.yml]
#
# ---- what this is, and what it deliberately is not -------------------------------------
#
# #1452 considered and REJECTED a lint for nonstandard identifiers, on the grounds that
# "a check that looks like a Windows build and is not one is worse than no check". This is
# not that check and must never grow into it. It compiles nothing, it reads no C++, and it
# has no opinion about `M_PI`. It asks one question about one file: CAN A PULL REQUEST
# STILL REACH `build-windows`, AND CAN IT STILL PUBLISH NOTHING WHEN IT GETS THERE?
#
# It exists because the answer can become "no" with no diff to the trigger and no error
# anywhere. GitHub SKIPS a job whose `needs` was skipped. So the day a precondition job is
# added above `build-windows` -- `signing-inputs` is on issue-1408 and is exactly this
# shape, and it cannot run on a fork's pull request because a fork's pull request is given
# no secrets at all -- `on: pull_request` goes on saying what it says, the checks list on
# every pull request shows a tidy grey "skipped", and the only thing that ever compiled
# this tree for MSVC quietly stops. That is the same silent direction as the empty
# organisation secret #1408 was filed about: not a failure, an absence.
#
# ---- the five rules --------------------------------------------------------------------
#
#  1. `pull_request` is a trigger of this workflow.
#  2. `build-windows` exists, and if it names anything in `needs:` it also carries a
#     job-level `if:` that mentions `pull_request`. That is not a style rule. A `needs:`
#     whose target is skipped on a pull request skips this job too, and the only way to
#     keep it running is an `if:` that says so out loud -- so the rule is that whoever adds
#     the `needs:` must say, in the same file, what happens to the pull-request build.
#  3. Every step that publishes -- anything using `action-gh-release` -- is gated on
#     `github.ref_type == 'tag'`. A pull_request run's ref is `refs/pull/N/merge` and its
#     ref_type is `branch`, so this is what makes "a pull request publishes nothing" true
#     rather than hoped for. It is #1299's property and #1452 inherits it.
#  4. No cache key names the event, the ref, the run or the commit. A cache entry belongs
#     to the ref that wrote it and is readable from that ref, its base ref and the default
#     branch; an event-scoped key would make every pull request rebuild static OpenCV from
#     source for eight and a half minutes, once each, for ever -- and the entry it then
#     wrote would be scoped to `refs/pull/N/merge` and help nobody, not even the next push.
#     The two free minutes #1452 was decided on are this rule.
#  5. A step that reads `secrets.ODPRIVATE` is unreachable on a pull request. A fork's
#     pull request is handed no secrets, so such a step does not degrade there -- it fails,
#     and it fails for the absence of something it was never going to be given. An empty
#     anchor is a perfectly good compile check: update_keys.hpp defaults both anchors to ""
#     and the artefact answers NoAnchor, which is the safe direction.
#
# ---- an empty census, and the one that may be empty -------------------------------------
#
# Four of these five can pass by looking at nothing, so each states what it found and a
# census that came back empty is a FAILURE rather than a clean sweep -- the shape
# release.yml itself uses on `dumpbin` output, and census.sh on this directory. The
# exception is rule 5: there is no step in this file that reads a signing key today, and
# the steps it is about arrive with #1408. So rule 5's census is allowed to be empty, it
# says so when it is, and it is the only one.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
YML="${1:-$ROOT/.github/workflows/release.yml}"

if [ ! -f "$YML" ]; then
    echo "FAIL: no workflow at $YML"
    exit 1
fi

python3 - "$YML" <<'PY'
import re
import sys

path = sys.argv[1]
raw = open(path, encoding="utf-8").read().splitlines()


def lex(lines):
    """(line_no, indent, key, value) for every mapping key, and ('-', rest) for sequence
    entries, with comments, blanks and block scalars skipped.

    A hand-written reader rather than PyYAML, because this check must run wherever bash
    and python3 do and nothing else in testers/ imports a third-party module. It reads
    less than YAML allows -- but it reads exactly the shapes this file is written in, and
    every rule below states what it found so a reader that quietly saw nothing is caught.
    """
    out = []
    skip_deeper_than = None
    for n, line in enumerate(lines, start=1):
        if skip_deeper_than is not None:
            if line.strip() == "" or (len(line) - len(line.lstrip(" "))) > skip_deeper_than:
                continue
            skip_deeper_than = None
        if line.strip() == "" or line.lstrip().startswith("#"):
            continue
        indent = len(line) - len(line.lstrip(" "))
        body = line.strip()
        if body.startswith("- "):
            rest = body[2:].strip()
            m = re.match(r"^([A-Za-z_][\w.-]*):\s*(.*)$", rest)
            if m:
                out.append((n, indent, "-" + m.group(1), m.group(2).strip()))
                if m.group(2).strip() in ("|", ">", "|-", ">-", "|+", ">+"):
                    skip_deeper_than = indent + 2
            else:
                out.append((n, indent, "-", rest))
            continue
        m = re.match(r"^([A-Za-z_][\w.-]*|\"[^\"]+\"|'[^']+'):\s*(.*)$", body)
        if m:
            value = m.group(2).strip()
            out.append((n, indent, m.group(1).strip("\"'"), value))
            if value in ("|", ">", "|-", ">-", "|+", ">+"):
                skip_deeper_than = indent
    return out


tokens = lex(raw)
faults = []
notes = []


def block(start_index, indent):
    """The tokens strictly inside the block whose header is tokens[start_index]."""
    got = []
    for n, ind, key, val in tokens[start_index + 1:]:
        if ind <= indent:
            break
        got.append((n, ind, key, val))
    return got


def find(key, indent):
    for i, (n, ind, k, v) in enumerate(tokens):
        if ind == indent and k == key:
            return i
    return None


# ---- rule 1: pull_request is a trigger --------------------------------------------------
on_at = find("on", 0)
if on_at is None:
    # `on` is the YAML 1.1 boolean the spec calls "y"; PyYAML would hand back True here and
    # this reader would not, which is one more reason it is hand-written. Say so either way.
    faults.append("rule 1: no top-level `on:` block was found in this file at all")
    triggers = []
else:
    triggers = [k for (_, ind, k, _) in block(on_at, 0) if ind == 2]
    if not triggers:
        faults.append("rule 1: the `on:` block named no trigger, so this check read nothing")
    elif "pull_request" not in triggers:
        faults.append(
            "rule 1: `pull_request` is not a trigger of this workflow. It names "
            + ", ".join(triggers)
            + ". #1452 built Windows on every pull request because five defects in one day "
            "were findable no other way; without this trigger nothing compiles this tree "
            "for MSVC until somebody dispatches a run by hand, which is what #1392's `M_PI` "
            "break cost."
        )
    else:
        notes.append("rule 1: triggers are " + ", ".join(triggers))

# ---- the jobs ---------------------------------------------------------------------------
jobs_at = find("jobs", 0)
jobs = {}
if jobs_at is None:
    faults.append("rule 2: no top-level `jobs:` block, so nothing below looked at anything")
else:
    inner = block(jobs_at, 0)
    for i, (n, ind, key, val) in enumerate(tokens):
        if ind == 2 and val == "" and any(t[0] == n for t in inner):
            jobs[key] = (i, block(i, 2), n)
    if len(jobs) < 2:
        faults.append(
            f"rule 2: this reader found {len(jobs)} job(s) in a file that has always had at "
            "least two. It is not reading this workflow, so nothing below it means anything."
        )
    else:
        notes.append("rule 2: jobs are " + ", ".join(sorted(jobs)))

# ---- rule 2: build-windows is reachable on a pull request --------------------------------
if jobs and "build-windows" not in jobs:
    faults.append(
        "rule 2: there is no `build-windows` job. If it was renamed, rename it here too; "
        "if it was removed, #1452 was reverted and that is a decision, not a diff."
    )
elif jobs:
    _, body, job_line = jobs["build-windows"]
    needs = [v for (_, ind, k, v) in body if ind == 4 and k == "needs"]
    job_if = [v for (_, ind, k, v) in body if ind == 4 and k == "if"]
    if needs:
        named = ", ".join(needs)
        if not job_if:
            faults.append(
                f"rule 2: `build-windows` (line {job_line}) names `needs: {named}` and "
                "carries no job-level `if:`. GitHub skips a job whose `needs` was skipped, "
                "and a precondition job that reads a secret CANNOT run on a fork's pull "
                "request -- a fork's pull request is given no secrets at all. So this is "
                "the shape that removes the pull-request Windows build while `on: "
                "pull_request` goes on saying it is there and the checks list shows a grey "
                "'skipped'. Give this job an `if:` that names `pull_request` and says what "
                "happens, for instance:\n"
                "    if: ${{ !cancelled() && (github.event_name == 'pull_request' || "
                "needs.signing-inputs.result == 'success') }}\n"
                "and give the precondition job `if: github.event_name != 'pull_request'`."
            )
        elif not any("pull_request" in c for c in job_if):
            faults.append(
                f"rule 2: `build-windows` (line {job_line}) names `needs: {named}` and its "
                "`if:` does not mention `pull_request`: " + " / ".join(job_if) + ". The "
                "`if:` has to answer the question the `needs:` raises, which is whether "
                "this job still runs when the job above it is skipped for want of a secret."
            )
        else:
            notes.append(
                f"rule 2: `build-windows` needs {named} and its `if:` names pull_request"
            )
    else:
        notes.append("rule 2: `build-windows` has no `needs:`, so nothing can skip it")

# ---- rule 3: publishing is gated on a tag ------------------------------------------------
# rule 5's census is gathered in the same sweep, because a step is where both answers are.
publishers = []
secret_steps = []
for job_name, (job_index, body, _) in jobs.items():
    steps_at = None
    for i, (n, ind, k, v) in enumerate(tokens):
        if ind == 4 and k == "steps" and any(t[0] == n for t in body):
            steps_at = i
            break
    if steps_at is None:
        faults.append(f"rule 3: job `{job_name}` has no `steps:` this reader could find")
        continue
    step_tokens = block(steps_at, 4)
    # A step begins at the token whose key was written on the `- ` line.
    starts = [i for i, t in enumerate(step_tokens) if t[2].startswith("-")]
    if not starts:
        faults.append(f"rule 3: job `{job_name}`'s `steps:` parsed to no steps at all")
        continue
    for si, s in enumerate(starts):
        end = starts[si + 1] if si + 1 < len(starts) else len(step_tokens)
        chunk = step_tokens[s:end]
        keys = {}
        for n, ind, k, v in chunk:
            keys.setdefault(k.lstrip("-"), []).append((n, v))
        uses = " ".join(v for _, v in keys.get("uses", []))
        name = (keys.get("name", [(0, "")])[0][1]) or uses
        ifs = [v for _, v in keys.get("if", [])]
        line = chunk[0][0]
        if "action-gh-release" in uses:
            publishers.append((job_name, name, line))
            if not any("ref_type" in c and "tag" in c for c in ifs):
                faults.append(
                    f"rule 3: the publishing step at line {line} in `{job_name}` is not "
                    "gated on `github.ref_type == 'tag'`. It reads: "
                    + (" / ".join(ifs) if ifs else "(no `if:` at all)")
                    + ". Every run of this workflow that is not a tag -- a dispatch and, "
                    "since #1452, every pull request -- would publish a release."
                )
        # rule 5, gathered here because a step is where the answer is.
        # The raw lines of this step, to the line before the next step begins -- a block
        # scalar's body is skipped by the lexer, so a token range alone would stop at
        # `run: |` and read none of the script under it.
        next_line = step_tokens[end][0] - 1 if end < len(step_tokens) else chunk[-1][0] + 200
        raw_chunk = "\n".join(raw[chunk[0][0] - 1:next_line])
        if "secrets.ODPRIVATE" in raw_chunk:
            job_if = [v for (_, ind, k, v) in body if ind == 4 and k == "if"]
            guarded = any(
                ("pull_request" in c) or ("ref_type" in c) for c in (ifs + job_if)
            )
            secret_steps.append((job_name, name, line, guarded, ifs + job_if))

if not publishers:
    faults.append(
        "rule 3: not one publishing step was found. This file has attached its own "
        "artefact to the release since #1299, so an empty census here means this check "
        "read nothing rather than that nothing publishes."
    )
else:
    notes.append(
        "rule 3: "
        + str(len(publishers))
        + " publishing step(s), all gated on a tag: "
        + "; ".join(f"{j}:{n} (line {l})" for j, n, l in publishers)
    )

# ---- rule 4: no cache key names the event, the ref, the run or the commit -----------------
cache_keys = []
for i, (n, ind, k, v) in enumerate(tokens):
    if k.lstrip("-") == "uses" and "actions/cache" in v:
        for n2, ind2, k2, v2 in tokens[i + 1:]:
            # Stop at the next step (a `- ` line) or at anything shallower: the `key:` we
            # want is under this step's own `with:`, which is DEEPER than the `uses:` line.
            if ind2 < ind or k2.startswith("-"):
                break
            if k2 == "key":
                cache_keys.append((n2, v2))
                break
if not cache_keys:
    faults.append(
        "rule 4: no `actions/cache` step with a `key:` was found. The static OpenCV tree "
        "has been restored rather than built since #1299, so an empty census here means "
        "this check did not look at anything."
    )
else:
    forbidden = ("github.event", "github.ref", "github.head_ref", "github.run_", "github.sha")
    for n, key in cache_keys:
        named = [f for f in forbidden if f in key]
        if named:
            faults.append(
                f"rule 4: the cache key at line {n} names {', '.join(named)}. A cache entry "
                "belongs to the ref that wrote it; scope this key to the event or the ref "
                "and every pull request rebuilds static OpenCV from source for eight and a "
                "half minutes, once each, for ever, and the entry it writes is scoped to "
                "`refs/pull/N/merge` where nothing else can read it. #1452's whole cost "
                "argument is that this does not happen."
            )
    notes.append(f"rule 4: {len(cache_keys)} cache key(s), none naming an event, ref or run")

# ---- rule 5: a signing key is never asked for on a pull request ---------------------------
if not secret_steps:
    notes.append(
        "rule 5: no step in this file reads secrets.ODPRIVATE. This is the one census here "
        "that may legitimately be empty -- the steps it is about arrive with #1408 -- and "
        "it says so rather than passing silently."
    )
else:
    for job_name, name, line, guarded, conditions in secret_steps:
        if guarded:
            notes.append(f"rule 5: {job_name}:{name} (line {line}) reads the signing key and is gated")
        else:
            faults.append(
                f"rule 5: the step at line {line} in `{job_name}` reads secrets.ODPRIVATE "
                "and nothing keeps it off a pull request: "
                + (" / ".join(conditions) if conditions else "neither it nor its job has an `if:`")
                + ". A fork's pull request is handed no secrets at all, so this step does "
                "not degrade there, it fails -- for the absence of something it was never "
                "going to be given. Gate it on `github.event_name != 'pull_request'`, or on "
                "the tag. A build with an empty anchor is still a valid compile check: "
                "update_keys.hpp defaults both anchors to \"\" and the artefact answers "
                "NoAnchor, which is the safe direction."
            )

for note in notes:
    print("  " + note)

if faults:
    print()
    for f in faults:
        print("FAIL: " + f)
    print()
    print(f"{len(faults)} fault(s) in {path}")
    sys.exit(1)

print()
print(f"the pull-request Windows build is reachable and publishes nothing ({path})")
PY
