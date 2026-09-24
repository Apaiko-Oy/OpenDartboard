#!/usr/bin/env python3
"""#1532: the update journey may run only where nothing publishes, and on every change
that could move it.

    testers/i1532_guard.py [tree]

Run as the FIRST step of .github/workflows/update-journey.yml, and by testers/run_all.sh
(label 1532-guard) so a Linux gate hears about a stale path list before a pull request
does. It asserts three things and refuses on any of them.

1. THE WORKFLOW THAT BUILDS THE THROWAWAY-ANCHOR LAUNCHER PUBLISHES NOTHING. The test
   launcher trusts a key whose private half existed for one step on one runner; it must
   never reach a release. release.yml's publishing jobs hold `contents: write` even on a
   pull request (measured in #1452, run 35513335239), so the check lives in a workflow of
   its own and this reads that file's CODE -- comments stripped -- for every way a job
   could publish or reach the release key: a write grant, any `secrets.` or `vars.`
   (ODPRIVATE and OD_UPDATE_ANCHOR_* live there), a release action, `gh release`, an
   uploaded artifact, a tag trigger. And it reads release.yml for the reverse: the
   publishing workflow must not run this check.

2. ON THE RUNNER, IT IS THAT WORKFLOW AND IT IS NOT A TAG. GITHUB_WORKFLOW_REF must name
   update-journey.yml and GITHUB_REF_TYPE must not be `tag`. Off a runner these are unset
   and only 1 and 3 are asked.

3. THE `paths:` FILTER COVERS WHAT THE LAUNCHER IS MADE OF. GitHub has no paths filter on
   a job, so the scoping is a workflow-level `on.pull_request.paths` -- and a list of
   globs goes stale the day the launcher includes a new header. So the list is measured
   rather than trusted: every file src/launcher/main.cpp reaches through a quoted
   #include, followed transitively, must match a glob in it, and so must the testers and
   the workflow the job itself runs.
"""

import os
import re
import sys

TREE = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JOURNEY = ".github/workflows/update-journey.yml"
RELEASE = ".github/workflows/release.yml"

failures = []


def note(passed, what):
    print(("ok   " if passed else "FAIL ") + what)
    if not passed:
        failures.append(what)


def code_of_yaml(text):
    """The YAML with every comment blanked: a `#` at the start or after whitespace."""
    out = []
    for line in text.splitlines():
        out.append(re.sub(r"(^|\s)#.*$", "", line))
    return "\n".join(out)


def glob_to_regex(glob):
    pattern = ""
    i = 0
    while i < len(glob):
        if glob.startswith("**", i):
            pattern += ".*"
            i += 2
        elif glob[i] == "*":
            pattern += "[^/]*"
            i += 1
        else:
            pattern += re.escape(glob[i])
            i += 1
    return re.compile("^" + pattern + "$")


def launcher_sources():
    """Every file src/launcher/main.cpp reaches through a quoted #include, transitively."""
    seen = set()
    todo = ["src/launcher/main.cpp"]
    include_dirs = ["src", "src/utils"]
    while todo:
        path = todo.pop()
        if path in seen:
            continue
        seen.add(path)
        text = open(os.path.join(TREE, path), encoding="utf-8").read()
        for name in re.findall(r'^\s*#\s*include\s+"([^"]+)"', text, re.M):
            candidates = [os.path.normpath(os.path.join(os.path.dirname(path), name))]
            candidates += [os.path.normpath(os.path.join(d, name)) for d in include_dirs]
            for candidate in candidates:
                if os.path.isfile(os.path.join(TREE, candidate)):
                    todo.append(candidate.replace(os.sep, "/"))
                    break
            else:
                note(False, "%s includes \"%s\", which is nowhere this guard looks" % (path, name))
    return sorted(seen)


def job_testers(journey):
    """The testers the job's steps name, and every tester those python files import.

    Read out of the workflow rather than listed here, so a step that starts using another
    tester is covered the day it is written -- and so that this guard, which run_all.sh
    runs, does not itself name the job's testers and read to testers/census.sh as running
    them.
    """
    # A name that is not a file is the paths glob `testers/i1532_*` read up to its star.
    found = sorted(one for one in set(re.findall(r"testers/[A-Za-z0-9_.]+", journey))
                   if os.path.isfile(os.path.join(TREE, one)))
    todo = [one for one in found if one.endswith(".py")]
    while todo:
        path = todo.pop()
        full = os.path.join(TREE, path)
        for module in re.findall(r"^import\s+(\w+)", open(full, encoding="utf-8").read(), re.M):
            candidate = "testers/" + module + ".py"
            if os.path.isfile(os.path.join(TREE, candidate)) and candidate not in found:
                found.append(candidate)
                todo.append(candidate)
    return sorted(found)


def main():
    journey_path = os.path.join(TREE, JOURNEY)
    if not os.path.isfile(journey_path):
        note(False, JOURNEY + " exists")
        return 1
    journey = code_of_yaml(open(journey_path, encoding="utf-8").read())
    release = code_of_yaml(open(os.path.join(TREE, RELEASE), encoding="utf-8").read())

    # ---- 1. nothing in the journey's workflow can publish or reach the release key -------
    print("---- 1. the workflow that builds a throwaway-anchor launcher publishes nothing ----")
    forbidden = [
        (r"contents:\s*write", "contents: write grant"),
        (r"\bsecrets\.", "secret (ODPRIVATE is one)"),
        (r"\bvars\.", "repository variable (the release anchors are two)"),
        (r"action-gh-release|gh\s+release|releases/", "release step"),
        (r"upload-artifact", "uploaded artifact, which is how a test launcher would leave the runner"),
        (r"\btags:", "tag trigger"),
        (r"\bpush:", "push trigger -- it runs on pull requests only"),
        (r"write-all", "blanket write grant"),
    ]
    for pattern, what in forbidden:
        hits = [line.strip() for line in journey.splitlines() if re.search(pattern, line)]
        note(not hits, "%s carries no %s%s" % (JOURNEY, what, (": " + " | ".join(hits)) if hits else ""))
    note(re.search(r"^permissions:\s*\n\s+contents:\s*read\s*$", journey, re.M) is not None,
         JOURNEY + " states `permissions: contents: read` at the top")
    # By prefix rather than by file name: this guard is reached by run_all.sh, and a tester's
    # name written in its code would count as running it (testers/census.sh, #1430).
    note(re.search(r"testers[/\\\\]i1532_", release) is None,
         RELEASE + " -- which publishes on a tag -- runs none of #1532's testers")

    # ---- 2. on the runner: this workflow, and not a tag --------------------------------------
    print("---- 2. where it is running ----")
    if os.environ.get("GITHUB_ACTIONS") == "true":
        workflow_ref = os.environ.get("GITHUB_WORKFLOW_REF", "")
        note("/" + JOURNEY + "@" in workflow_ref, "the running workflow is %s (GITHUB_WORKFLOW_REF=%s)"
             % (JOURNEY, workflow_ref))
        note(os.environ.get("GITHUB_REF_TYPE") != "tag", "the ref is not a tag (GITHUB_REF_TYPE=%s)"
             % os.environ.get("GITHUB_REF_TYPE"))
        note(os.environ.get("GITHUB_EVENT_NAME") == "pull_request", "the event is a pull request "
             "(GITHUB_EVENT_NAME=%s)" % os.environ.get("GITHUB_EVENT_NAME"))
    else:
        print("     (not on a runner: only the files are asked)")

    # ---- 3. the paths filter covers the launcher ----------------------------------------------
    print("---- 3. the pull_request paths filter covers everything the launcher is made of ----")
    block = re.search(r"^\s+paths:\s*\n((?:\s+-\s+.*\n?)+)", journey, re.M)
    globs = re.findall(r"-\s+[\"']?([^\"'\s]+)", block.group(1)) if block else []
    note(bool(globs), JOURNEY + " has an on.pull_request.paths list (%d globs)" % len(globs))
    patterns = [glob_to_regex(g) for g in globs]
    sources = launcher_sources()
    print("     the launcher reaches %d files through its includes" % len(sources))
    job_files = job_testers(journey)
    print("     the job runs %d testers: %s" % (len(job_files), ", ".join(job_files)))
    wanted = sources + job_files + [JOURNEY]
    uncovered = [one for one in wanted if not any(p.match(one) for p in patterns)]
    for one in uncovered:
        note(False, "a change to %s would not run the journey: no glob in paths: matches it" % one)
    note(not uncovered, "every one of the %d is matched by a glob in paths:" % len(wanted))
    note(any(p.match("src/launcher/anything.hpp") for p in patterns)
         and any(p.match("src/update/anything.hpp") for p in patterns),
         "and the two the issue names, src/launcher/ and src/update/, are matched whole")

    print()
    if failures:
        print("REFUSED: %d of the guard's questions answered wrong; the journey does not run" % len(failures))
        return 1
    print("GUARD_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
