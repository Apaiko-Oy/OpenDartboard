#!/usr/bin/env bash
# #1189: what a second device on the board's network sees - run from the fork's root on a
# host with Docker, after the binary is built into build/.
#
# check_announce.py asks the board's own Avahi what it publishes. That proves the file and
# the daemon agree; it does not prove anything left the machine, because avahi-browse on
# the same host answers from the local daemon. This script puts the board in one container
# and a peer in another on a user-defined bridge network - a different network namespace,
# which is what a phone on the club's wifi is - and asks the peer, with two resolvers that
# share no code (Avahi's avahi-browse and python3-zeroconf), in three states:
#
#   board --listen, running   -> the peer resolves the label, port 13520, path=/scores
#   board --listen, stopped   -> the peer resolves nothing (the file is gone, Avahi says goodbye)
#   board loopback, running   -> the peer resolves nothing
#
# and in every state, that the token appears in nothing the peer received.
#
#     bash tools/announce/sibling_probe.sh
#
# Containers are named $PREFIX-board and $PREFIX-peer, the network $NET; all are removed on
# exit. The image needs avahi-daemon, avahi-utils, dbus and python3-zeroconf.
set -u

IMAGE=${IMAGE:-opendartboard:i1189}
PREFIX=${PREFIX:-od-i1189}
NET=${NET:-od-i1189-net}
LABEL=${LABEL:-Kello 2 sibling}
PORT=13520
FAILED=0
ROOT=$(pwd)

AVAHI='mkdir -p /run/dbus && rm -f /run/dbus/pid && dbus-daemon --system --fork && avahi-daemon -D --no-rlimits'

cleanup() {
    docker rm -f "$PREFIX-board" "$PREFIX-peer" >/dev/null 2>&1
    docker network rm "$NET" >/dev/null 2>&1
}
trap cleanup EXIT

record() { # what expected got detail
    if [ "$2" = "$3" ]; then tag="ok  "; else tag="FAIL"; FAILED=$((FAILED + 1)); fi
    printf '  %s  %-58s expected %-8s got %-8s %s\n' "$tag" "$1" "$2" "$3" "${4:-}"
}

start_board() { # extra flags
    docker run -d --rm --name "$PREFIX-board" --cpus=2 --network "$NET" \
        -v "$ROOT":/app -w /app -e OD_SHUTDOWN_FIX=1 -e OD_MAX_CYCLES=1000000 "$IMAGE" \
        bash -c "$AVAHI && mkdir -p /runs/sib && cd /runs/sib && { /app/build/opendartboard --debug \
            --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 --width 1280 --height 720 \
            --label '$LABEL' $* & echo \$! > /runs/sib/detector.pid; wait; echo DETECTOR_EXITED; sleep 600; }" >/dev/null
    for _ in $(seq 1 180); do
        if docker logs "$PREFIX-board" 2>&1 | grep -q 'listening on ws://'; then return 0; fi
        sleep 1
    done
    return 1
}

peer() { # what the peer resolves, both resolvers, one blob
    docker run --rm --name "$PREFIX-peer" --cpus=1 --network "$NET" -v "$ROOT":/app "$IMAGE" bash -c \
        "python3 /app/tools/announce/browse_from_sibling.py --seconds 6; $AVAHI && sleep 4 && echo '--- avahi-browse' && avahi-browse -rtp _opendartboard._tcp" 2>&1
}

ask() { # state expected(present|absent) token
    local out found port
    out=$(peer)
    echo "$out" | grep -E "^board |^no board|^=;" | sed 's/^/        /'
    if echo "$out" | grep -q "^board '$LABEL'"; then found=present; else found=absent; fi
    record "$1: zeroconf on the peer resolves '$LABEL'" "$2" "$found"
    if echo "$out" | grep -q "^=;.*;IPv4;$(echo "$LABEL" | sed 's/ /\\\\032/g');"; then found=present; else found=absent; fi
    record "$1: avahi-browse on the peer resolves '$LABEL'" "$2" "$found"
    if [ "$2" = present ]; then
        port=$(echo "$out" | grep "^=;.*;IPv4;" | head -1 | cut -d';' -f9)
        record "$1: the resolved port is the socket's" "$PORT" "${port:-none}"
        if echo "$out" | grep "^=;.*;IPv4;" | grep -q '"path=/scores"'; then found=yes; else found=no; fi
        record "$1: the resolved txt names path=/scores" yes "$found"
    fi
    if [ -n "$3" ] && echo "$out" | grep -qF "$3"; then found=present; else found=absent; fi
    record "$1: the token is in nothing the peer received" absent "$found"
}

cleanup
docker network create "$NET" >/dev/null || exit 1
echo "sibling probe v1   image $IMAGE   network $NET   label '$LABEL'"

echo "state 1: board --listen, running"
start_board --listen || { record "the board's socket opens" open closed; exit 1; }
TOKEN=$(docker exec "$PREFIX-board" cat /runs/sib/score_token 2>/dev/null)
record "the board wrote its service file" present \
    "$(docker exec "$PREFIX-board" test -f /etc/avahi/services/opendartboard.service && echo present || echo absent)"
sleep 3
ask "listen" present "$TOKEN"

echo "state 2: board --listen, detector stopped with SIGINT, the board's Avahi still running"
# The detector is signalled by its own pid and the container stays up, so Avahi on the
# board keeps answering: an absence below is a withdrawal, not a host that went away.
docker exec "$PREFIX-board" bash -c 'kill -INT "$(cat /runs/sib/detector.pid)"'
for _ in $(seq 1 30); do
    docker logs "$PREFIX-board" 2>&1 | grep -q DETECTOR_EXITED && break
    sleep 1
done
LOG=$(docker logs "$PREFIX-board" 2>&1)
echo "$LOG" | grep -E "announced as|announcement|Received signal|DETECTOR_EXITED" | sed 's/^/        /'
record "the board's Avahi is still running" yes \
    "$(docker exec "$PREFIX-board" avahi-daemon --check && echo yes || echo no)"
record "the board's service file is gone" absent \
    "$(docker exec "$PREFIX-board" test -f /etc/avahi/services/opendartboard.service && echo present || echo absent)"
if [ -n "$TOKEN" ] && echo "$LOG" | grep -qF "$TOKEN"; then found=present; else found=absent; fi
record "the token is in no line of the board's log" absent "$found"
ask "stopped" absent "$TOKEN"
docker rm -f "$PREFIX-board" >/dev/null 2>&1

echo "state 3: board loopback, running"
start_board || { record "the board's socket opens" open closed; exit 1; }
TOKEN=$(docker exec "$PREFIX-board" cat /runs/sib/score_token 2>/dev/null)
record "the board wrote no service file" absent \
    "$(docker exec "$PREFIX-board" test -f /etc/avahi/services/opendartboard.service && echo present || echo absent)"
sleep 3
ask "loopback" absent "$TOKEN"
docker exec "$PREFIX-board" bash -c 'kill -INT "$(cat /runs/sib/detector.pid)"' >/dev/null 2>&1

echo "failed $FAILED"
if [ "$FAILED" -eq 0 ]; then echo PASS; else echo "FAIL: $FAILED check(s) did not hold"; fi
[ "$FAILED" -eq 0 ]
