#!/bin/bash
# #1659: does the .deb `make deb` builds carry what the units need, and do its maintainer
# scripts enable, disable and keep a board's state the way they say?
#
# unrun-tester: not in run_all.sh. It measures packaging, not the detector, and runs in a
# minute; it needs docker and $OD_IMAGE and nothing else.
#
#   testers/i1659_deb_check.sh
#   OD_1659_TREE=<a checkout> testers/i1659_deb_check.sh   # package another tree, e.g.
#       `git archive 52728ef Makefile distributions templates scripts models | tar -x -C d`
#       -- main before #1659, which must be red
#
# THE DEFECT. `make deb` staged DEBIAN/control and /usr/local/bin/opendartboard and nothing
# else, so the package CI publishes installed a binary and no unit, and nothing started the
# detector at boot -- while postinst, postrm and both unit templates sat in the tree.
#
# What it measures, in two throwaway containers of $OD_IMAGE:
#
#   1 build    `make deb` in the tree, the way release.yml's build-deb step runs it. The
#              binary is build/opendartboard when there is one and a stub otherwise:
#              which one is printed, and nothing below reads the binary.
#   2 package  `dpkg-deb -c` holds every path the units name, with the modes they need;
#              `dpkg-deb -I` holds postinst, prerm and postrm; the rendered units carry no
#              unreplaced ${...}, keep $STATE_DIRECTORY in their comments, and the detector
#              and lock_cams.sh are handed the same mode. #1801: the tmpfiles.d rule is
#              carried, and the unit's ExecStart names --autocams and --log-file.
#   3 install  The package into a fresh container with /score_token planted where a
#              pre-#1660 unit left it. There is no booted systemd in a container, so
#              postinst's enable is measured (it writes symlinks offline) and its
#              daemon-reload/restart are not. Both units must be enabled, the state
#              directory 0700 and holding the moved token; `systemd-analyze verify` on the
#              installed units; a second configure must not move a newer stray over it.
#              Then `dpkg -r`: units gone, disabled, state kept. Then `dpkg -P`: state gone.
#
# The package is Architecture: arm64 and this image is amd64, so the install runs with
# --force-architecture --force-depends: dpkg is asked to place the files and run the
# scripts, which is what is measured, and not to resolve arm64 libraries.
#
# It ends on an exit status, not on an echo (#1463).
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

TREE="${OD_1659_TREE:-$OD_TREE_ROOT}"
TREE="$(cd "$TREE" && pwd)"
RUN="$OD_RUNS_BASE/1659/check"
VERSION=0.0.0-check

rm -rf "$RUN" && mkdir -p "$RUN/out" || exit 2
if ! docker image inspect "$OD_IMAGE" > /dev/null 2>&1; then
  echo "i1659: the image $OD_IMAGE is not on this machine" >&2
  exit 2
fi

BIN_MOUNT=()
if [ -x "$OD_TREE_ROOT/build/opendartboard" ]; then
  echo "binary: $OD_TREE_ROOT/build/opendartboard"
  BIN_MOUNT=(-v "$OD_TREE_ROOT/build/opendartboard:/stage/opendartboard:ro")
else
  echo "binary: a stub (no build/opendartboard in this checkout; nothing here runs it)"
fi

# ---- 1 build ------------------------------------------------------------------------------
echo "--- 1 build: make deb in $TREE ---"
docker rm -f "$(od_name 1659-build)" > /dev/null 2>&1
if ! docker run --rm --name "$(od_name 1659-build)" --network none \
    -v "$TREE":/src:ro -v "$RUN/out":/out "${BIN_MOUNT[@]}" "$OD_IMAGE" bash -c '
      set -e
      cp -r /src /work && cd /work && rm -rf dist
      if [ -f /stage/opendartboard ]; then cp /stage/opendartboard /usr/local/bin/opendartboard
      else printf "#!/bin/sh\nexit 0\n" > /usr/local/bin/opendartboard; chmod 755 /usr/local/bin/opendartboard; fi
      make deb VERSION='"$VERSION"' > /out/make.log 2>&1
      cp dist/*.deb /out/
      chown -R '"$(id -u):$(id -g)"' /out' ; then
  echo "FAIL make deb did not build; see $RUN/out/make.log"
  tail -20 "$RUN/out/make.log"
  exit 1
fi
DEB="$(ls "$RUN"/out/*.deb)"
echo "built $(basename "$DEB")"

# ---- 2 package and 3 install, inside one fresh container ----------------------------------
echo "--- 2 package, 3 install ---"
docker rm -f "$(od_name 1659-install)" > /dev/null 2>&1
docker run --rm --name "$(od_name 1659-install)" --network none \
    -v "$DEB":/pkg.deb:ro "$OD_IMAGE" bash -c '
FAILED=0
fail() { echo "FAIL $*"; FAILED=1; }
ok()   { echo "ok   $*"; }

echo "## dpkg-deb -c"; dpkg-deb -c /pkg.deb
echo "## dpkg-deb -I"; dpkg-deb -I /pkg.deb | sed -n "1,/^ Package:/p"
listing="$(dpkg-deb -c /pkg.deb)"
has() { # mode path
  if echo "$listing" | awk -v m="$1" -v p="./$2" "\$1==m && \$6==p {f=1} END {exit !f}"; then
    ok "carries $1 $2"; else fail "does not carry $1 $2"; fi
}
has -rwxr-xr-x usr/local/bin/opendartboard
has -rwxr-xr-x usr/local/bin/lock_cams.sh
has -rw-r--r-- lib/systemd/system/opendartboard.service
has -rw-r--r-- lib/systemd/system/lock_cams.service
has -rw-r--r-- usr/local/share/opendartboard/models/dart.param
has -rw-r--r-- usr/local/share/opendartboard/models/dart.bin
has -rw-r--r-- usr/lib/tmpfiles.d/opendartboard.conf
if echo "$listing" | awk "\$2!=\"root/root\" {f=1} END {exit f}"; then ok "every file root/root"
else fail "a file is not root/root"; fi
info="$(dpkg-deb -I /pkg.deb)"
for s in postinst prerm postrm; do
  if echo "$info" | awk -v s="$s" "\$2==\"bytes,\" && \$NF!=s && \$6==s {f=1} END {exit !f}"; then ok "control has $s"
  else fail "control has no $s"; fi
done

mkdir -p /x && dpkg-deb -x /pkg.deb /x
for u in opendartboard lock_cams; do
  f=/x/lib/systemd/system/$u.service
  [ -f "$f" ] || continue
  if awk "/\\\$\\{/ {f=1} END {exit f}" "$f"; then ok "$u.service: no unreplaced \${...}"
  else fail "$u.service: an unreplaced \${...}"; fi
done
f=/x/lib/systemd/system/opendartboard.service
if [ -f "$f" ] && awk "/export it as \\\$STATE_DIRECTORY/ {f=1} END {exit !f}" "$f"; then
  ok "opendartboard.service comments keep \$STATE_DIRECTORY (envsubst was restricted)"
else fail "opendartboard.service lost \$STATE_DIRECTORY from its comments"; fi
det="$(awk "/--width/ {print \$2\"x\"\$4\"@\"\$6}" "$f" 2>/dev/null)"
lock="$(awk -F"[= ]" "/^Environment=WIDTH/ {print \$3\"x\"\$5\"@\"\$7}" /x/lib/systemd/system/lock_cams.service 2>/dev/null)"
if [ -n "$det" ] && [ "$det" = "$lock" ]; then ok "detector and lock_cams.sh both run at $det"
else fail "detector mode \"$det\" and lock_cams mode \"$lock\" differ"; fi

# #1801: the unit starts the detector with --autocams and a log file per start, and the
# package carries the rule that prunes them. testers/i1801_unit_log_check.sh runs both.
if awk "/^ExecStart=/ {on=1} on {print} on && !/\\\\\$/ {exit}" "$f" | awk "/--autocams/ {a=1} /--log-file/ {l=1} /--cams / {c=1} END {exit !(a && l && !c)}"; then
  ok "opendartboard.service starts the detector with --autocams and --log-file, and no --cams"
else fail "opendartboard.service does not start the detector with --autocams and --log-file"; fi
if awk "/^e .*\/var\/lib\/opendartboard\/logs / {e=1} /^x .*\/var\/lib\/opendartboard\/logs\/current\$/ {x=1} END {exit !(e && x)}" /x/usr/lib/tmpfiles.d/opendartboard.conf 2>/dev/null; then
  ok "tmpfiles.d/opendartboard.conf ages logs/ and excludes logs/current/"
else fail "tmpfiles.d/opendartboard.conf does not age logs/ with logs/current/ excluded"; fi

echo "## install, with /score_token and /channel.json where a pre-#1660 unit left them"
printf "old-token\n" > /score_token; printf "{}\n" > /channel.json
if dpkg -i --force-architecture --force-depends /pkg.deb > /tmp/i.log 2>&1; then ok "dpkg -i"
else fail "dpkg -i"; cat /tmp/i.log; fi
grep "opendartboard:" /tmp/i.log
W=/etc/systemd/system/multi-user.target.wants
for u in opendartboard lock_cams; do
  if [ -L $W/$u.service ]; then ok "$u.service enabled"; else fail "$u.service not enabled"; fi
done
ls -l $W/ 2>/dev/null | awk "NR>1"
if [ "$(stat -c %a /var/lib/opendartboard 2>/dev/null)" = 700 ]; then ok "/var/lib/opendartboard is 0700"
else fail "/var/lib/opendartboard is not 0700"; fi
if [ "$(cat /var/lib/opendartboard/score_token 2>/dev/null)" = old-token ] && [ ! -e /score_token ]; then
  ok "/score_token moved into the state directory"; else fail "/score_token not moved"; fi
if [ -f /var/lib/opendartboard/channel.json ] && [ ! -e /channel.json ]; then
  ok "/channel.json moved into the state directory"; else fail "/channel.json not moved"; fi
if systemd-analyze verify /lib/systemd/system/opendartboard.service /lib/systemd/system/lock_cams.service > /tmp/v.log 2>&1 \
   && ! grep -qi "is not executable\|No such file" /tmp/v.log; then ok "systemd-analyze verify on the installed units ($(wc -l < /tmp/v.log) lines of complaint)"; cat /tmp/v.log
else fail "systemd-analyze verify"; cat /tmp/v.log; fi

echo "## configure again, with a newer stray /score_token"
printf "newer-stray\n" > /score_token
dpkg -i --force-architecture --force-depends /pkg.deb > /tmp/i2.log 2>&1 || fail "second dpkg -i"
if [ "$(cat /var/lib/opendartboard/score_token)" = old-token ] && [ -f /score_token ]; then
  ok "a second configure does not overwrite the kept token"; else fail "the kept token was overwritten"; fi
rm -f /score_token

echo "## dpkg -r"
dpkg -r opendartboard > /tmp/r.log 2>&1 || { fail "dpkg -r"; cat /tmp/r.log; }
for u in opendartboard lock_cams; do
  if [ ! -e $W/$u.service ] && [ ! -L $W/$u.service ]; then ok "$u.service disabled"; else fail "$u.service still enabled"; fi
  if [ ! -e /lib/systemd/system/$u.service ]; then ok "$u.service file removed"; else fail "$u.service file left"; fi
done
if [ ! -e /usr/local/bin/lock_cams.sh ] && [ ! -e /usr/local/bin/opendartboard ]; then ok "binaries removed"
else fail "a binary was left"; fi
if [ -f /var/lib/opendartboard/score_token ]; then ok "remove keeps the state directory"
else fail "remove deleted the state directory"; fi

echo "## dpkg -P"
dpkg -P opendartboard > /tmp/p.log 2>&1 || { fail "dpkg -P"; cat /tmp/p.log; }
if [ ! -e /var/lib/opendartboard ]; then ok "purge deletes the state directory"
else fail "purge left the state directory"; fi

exit $FAILED'
status=$?
if [ $status -eq 0 ]; then echo "i1659: PASS"; else echo "i1659: FAIL"; fi
exit $status
