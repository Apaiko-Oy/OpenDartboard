# Sourced by every harness in this directory. No tester names a worktree.
#
# Until #1335 each harness carried the absolute path of the tree it was written in
# (/home/mikko/opendartboard/i1320, i1318, ...), so running a sibling's tester meant
# copying it to scratch and repointing it by hand. Three agents each invented that
# workaround separately, and while they did, two testers went red on main and nobody
# saw it. A tester now runs from whatever checkout it is in.
#
#   OD_TREE_ROOT   the checkout, derived from this file's own location
#   OD_TREE_TAG    that checkout's name, reduced to what a container name may hold
#   OD_RUNS_BASE   where run directories go: beside the tree, named after it, so two
#                  checkouts on one box cannot write into each other's run. Override
#                  with OD_RUNS.
#   OD_IMAGE       the image every harness runs in. Override with OD_IMAGE.
#   od_name        a container name unique to this checkout
#   od_still       a still JPEG for the phases that hand the detector a frozen frame
#
# Sourced, never executed: it defines and sets, and runs nothing.

OD_TREE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OD_TREE_TAG="$(basename "$OD_TREE_ROOT")"
OD_TREE_TAG="${OD_TREE_TAG//[^A-Za-z0-9_.-]/-}"   # a container name holds no more than this
OD_RUNS_BASE="${OD_RUNS:-$(dirname "$OD_TREE_ROOT")/runs-$OD_TREE_TAG}"
OD_IMAGE="${OD_IMAGE:-od-amd64:bullseye}"

# A container name carrying the checkout, so two trees running the same tester at once
# do not collide on the name each reaps by.
od_name() { echo "od-$OD_TREE_TAG-$1"; }

# The blind-camera phases hand the detector a JPEG where a camera should be. The file
# used to be /tmp/still892.jpg -- which a reboot removes -- and then one run directory's
# copy of it, which only exists on the box that ran that issue. It is a frame of the
# shipped mocks, so it is made from them, once per box, and cached beside the runs.
od_still() {
  local dest="$1"
  local cache="${OD_STILL:-$OD_RUNS_BASE/fixtures/still.jpg}"
  if [ ! -s "$cache" ]; then
    mkdir -p "$(dirname "$cache")" || return 1
    docker run --rm --name "$(od_name still)" --network none \
      -v "$OD_TREE_ROOT":/app -v "$(dirname "$cache")":/fixtures "$OD_IMAGE" bash -c '
        g++ -std=c++17 -O1 -o /tmp/still /app/testers/still_frame.cpp \
          $(pkg-config --cflags --libs opencv4) || exit 1
        /tmp/still /app/mocks/cam_1.mp4 /fixtures/'"$(basename "$cache")"' 30' > /dev/null 2>&1 || return 1
  fi
  cp "$cache" "$dest"
}
