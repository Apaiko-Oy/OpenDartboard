#!/bin/bash
# #1408: what scripts/sign-manifest.sh writes, and every shape it refuses to write.
#
# WHY THIS EXISTS. The writing half of the update path lives in a workflow that runs on a
# `v*` tag and nowhere else, so until somebody pushes a tag nothing in this repository has
# ever executed it -- and the failure it would produce is the worst one available: an
# unsigned or malformed manifest attached to a real release, which every board on the
# channel then refuses, or worse, accepts. The signing itself is therefore a script rather
# than ten lines of YAML, and this is that script measured on any machine with openssl.
#
# WHAT IT CANNOT MEASURE, said here rather than discovered: it does not compile anything
# and it does not run the detector, so "src/update/manifest.hpp accepts this envelope" is
# not asserted here. It is asserted by release.yml's own step, which runs the artefact
# that release ships against the manifest that release ships, and by testers/i1305_run.sh,
# which builds a binary with a fixture anchor and feeds it a manifest over a socket. What
# IS measured here is everything about the envelope that a reader can refuse: the base64
# the strict decoder in manifest.hpp will accept, the DER both readers parse, the field
# names and the digest, and the signature verifying over the payload BYTES.
#
# Every assertion below has a control, because an absence that was never planted is not a
# finding: the signature is checked to verify, and then the payload is moved by one byte
# and checked to stop verifying.
#
#   testers/i1408_sign_check.sh [worktree]
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="${1:-$OD_TREE_ROOT}"
SIGN="$TREE/scripts/sign-manifest.sh"

command -v openssl > /dev/null || { echo "i1408: openssl is not on this machine, and it is the whole subject" >&2; exit 2; }
[ -f "$SIGN" ] || { echo "i1408: there is no $SIGN" >&2; exit 2; }

FAILURES=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAILURES=$((FAILURES + 1)); fi; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

VERSION=0.1.4
URL="https://github.com/Apaiko-Oy/OpenDartboard/releases/download/v$VERSION/opendartboard-$VERSION-windows-x64.zip"

# ---- a throwaway pair, and the anchor that is its public half ----------------------------
# Never a trust anchor and never committed: ECDSA signing is randomised, so a fixture's
# bytes are not stable and pinning them would be pinning nothing. What is stable is the
# relationship between the files, which is what this asserts.
openssl ecparam -name prime256v1 -genkey -noout -out "$WORK/signing.pem" 2> /dev/null
openssl ecparam -name prime256v1 -genkey -noout -out "$WORK/stranger.pem" 2> /dev/null
openssl ecparam -name secp384r1 -genkey -noout -out "$WORK/toobig.pem" 2> /dev/null
openssl ec -in "$WORK/signing.pem" -pubout -out "$WORK/public.pem" 2> /dev/null

anchor_of() {
  openssl ec -pubin -in "$1" -text -noout 2> /dev/null |
    awk '/pub:/{r=1; next} r{ if ($0 !~ /:/ || $0 ~ /ASN1|NIST/) exit; gsub(/[^0-9a-f]/,""); printf "%s", $0 }' |
    sed 's/^04//'
}
ANCHOR="$(anchor_of "$WORK/public.pem")"
[ "${#ANCHOR}" -eq 128 ]; note $? "the throwaway key's public half is a 128-character anchor"

# An artefact of known bytes. The zip a release really ships is 38 MB of OpenCV; what is
# being measured here is the digest and the length, and those do not care.
head -c 65536 /dev/urandom > "$WORK/opendartboard-$VERSION-windows-x64.zip"
ART="$WORK/opendartboard-$VERSION-windows-x64.zip"
DIGEST="$(openssl dgst -sha256 -r "$ART" | cut -d' ' -f1)"
SIZE="$(wc -c < "$ART" | tr -d '[:space:]')"

# ---- the manifest it writes ---------------------------------------------------------------
sign() {
  bash "$SIGN" --channel "$1" --version "$2" --url "$3" --artefact "$4" \
    --key "$5" --anchor "$6" --minimum-launcher "$7" --out "$8"
}

OUT="$WORK/manifest.json"
sign stable "$VERSION" "$URL" "$ART" "$WORK/signing.pem" "$ANCHOR" 0.0.1 "$OUT" > "$WORK/sign.log" 2>&1
note $? "sign-manifest.sh signs a stable release"
sed 's/^/     | /' "$WORK/sign.log"
[ -s "$OUT" ]; note $? "  and writes a manifest"

PAYLOAD_B64="$(sed -n 's/.*"payload": "\([^"]*\)".*/\1/p' "$OUT")"
SIGNATURE_B64="$(sed -n 's/.*"signature": "\([^"]*\)".*/\1/p' "$OUT")"
[ -n "$PAYLOAD_B64" ] && [ -n "$SIGNATURE_B64" ]
note $? "the envelope is {\"payload\": \"<base64>\", \"signature\": \"<base64>\"} and nothing else"

# manifest.hpp::base64Decode is strict: it refuses any character outside the alphabet, any
# data after padding, and any length that is not a multiple of four. A base64 written with
# line wrapping passes a lenient decoder and is refused by that one, which is a manifest
# nobody can read and a release nobody can undo. So the shape is asserted, not assumed.
strict_base64() {
  printf '%s' "$2" | grep -Eq '^[A-Za-z0-9+/]+={0,2}$'
  note $? "  the $1 is base64 the strict decoder in manifest.hpp accepts"
  [ $(( ${#2} % 4 )) -eq 0 ]
  note $? "  and its length is a multiple of four, which that decoder requires"
}
strict_base64 payload "$PAYLOAD_B64"
strict_base64 signature "$SIGNATURE_B64"

printf '%s' "$PAYLOAD_B64" | openssl base64 -d -A > "$WORK/payload.json" 2> /dev/null
note $? "the payload decodes"

EXPECTED="{\"channel\":\"stable\",\"version\":\"$VERSION\",\"url\":\"$URL\",\"sha256\":\"$DIGEST\",\"size\":$SIZE,\"minimumLauncher\":\"0.0.1\"}"
[ "$(cat "$WORK/payload.json")" = "$EXPECTED" ]
note $? "  to exactly the six fields ADR-0077 names, with this artefact's own digest and length"
if [ "$(cat "$WORK/payload.json")" != "$EXPECTED" ]; then
  echo "     | wrote:    $(cat "$WORK/payload.json")"
  echo "     | expected: $EXPECTED"
fi

# A trailing newline inside the payload would be signed and would be part of the version
# string nothing else in this repository writes with one. It is also the sort of thing a
# `>` redirect adds without being asked, so it is measured rather than trusted.
[ "$(tail -c 1 "$WORK/payload.json" | od -An -c | tr -d ' \n')" != '\n' ]
note $? "  and the signed bytes end where the JSON does"

printf '%s' "$SIGNATURE_B64" | openssl base64 -d -A > "$WORK/signature.der" 2> /dev/null
openssl asn1parse -inform DER -in "$WORK/signature.der" 2> /dev/null | head -1 | grep -q SEQUENCE
note $? "the signature is a DER SEQUENCE, which is what manifest.hpp::derSignature parses"

# ---- it verifies, and it stops verifying when a byte moves --------------------------------
openssl dgst -sha256 -verify "$WORK/public.pem" -signature "$WORK/signature.der" "$WORK/payload.json" > /dev/null 2>&1
note $? "the signature verifies over the payload bytes under the anchor's own key"

sed "s/\"size\":$SIZE/\"size\":$((SIZE + 1))/" "$WORK/payload.json" > "$WORK/moved.json"
[ "$(cat "$WORK/moved.json")" != "$(cat "$WORK/payload.json")" ]
note $? "  the control really moved a byte"
openssl dgst -sha256 -verify "$WORK/public.pem" -signature "$WORK/signature.der" "$WORK/moved.json" > /dev/null 2>&1
[ $? -ne 0 ]
note $? "  and one byte of the payload is enough to stop it verifying"

openssl dgst -sha256 -verify <(openssl ec -in "$WORK/stranger.pem" -pubout 2> /dev/null) \
  -signature "$WORK/signature.der" "$WORK/payload.json" > /dev/null 2>&1
[ $? -ne 0 ]
note $? "  and another key's verdict on the same bytes is no"

# ---- a beta release says beta inside the signature -----------------------------------------
sign beta 0.2.0-rc1 "$URL" "$ART" "$WORK/signing.pem" "$ANCHOR" 0.0.1 "$WORK/beta.json" > /dev/null 2>&1
note $? "a prerelease signs for the beta channel"
sed -n 's/.*"payload": "\([^"]*\)".*/\1/p' "$WORK/beta.json" | openssl base64 -d -A 2> /dev/null | grep -q '"channel":"beta"'
note $? "  and the channel is inside the signature, where readForChannel looks for it"

# ---- what it refuses, and every one of them writes nothing ----------------------------------
refuses() {
  local why="$1"; shift
  local out="$WORK/refused-$(echo "$why" | tr -cd 'a-z').json"
  sign "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$out" > "$WORK/refusal.log" 2>&1
  local rc=$?
  if [ "$rc" -eq 0 ]; then
    note 1 "$why"
    sed 's/^/     | /' "$WORK/refusal.log"
    return
  fi
  [ ! -e "$out" ]
  note $? "$why"
  sed 's/^/     | /' "$WORK/refusal.log"
}

refuses "a signing key whose public half is not this release's anchor is refused by name" \
  stable "$VERSION" "$URL" "$ART" "$WORK/stranger.pem" "$ANCHOR" 0.0.1
refuses "a key that is not P-256 is refused, because CNG verifies P-256 and nothing else" \
  stable "$VERSION" "$URL" "$ART" "$WORK/toobig.pem" "$ANCHOR" 0.0.1
refuses "a version with a leading v is refused, because a board compares it to what --version prints" \
  stable "v$VERSION" "$URL" "$ART" "$WORK/signing.pem" "$ANCHOR" 0.0.1
refuses "an http url is refused (ADR-0077 §6)" \
  stable "$VERSION" "http://example.test/x.zip" "$ART" "$WORK/signing.pem" "$ANCHOR" 0.0.1
refuses "a third channel word is refused" \
  nightly "$VERSION" "$URL" "$ART" "$WORK/signing.pem" "$ANCHOR" 0.0.1
# ${ANCHOR:1} rather than a hand-written string, so the refusal is measured against a
# value that is otherwise exactly right: 127 of the correct 128 characters. The first
# spelling of this case was ${ANCHOR#00}, which strips nothing from an anchor that does
# not start with 00 -- so it passed the real anchor in and the case went green by signing
# successfully. A mutation that does not mutate is a test that cannot fail.
SHORT_ANCHOR="${ANCHOR:1}"
[ "${#ANCHOR}" -eq 128 ] && [ "${#SHORT_ANCHOR}" -eq 127 ]
note $? "the anchor this next case shortens really is one character shorter"
refuses "an anchor that is not 128 hexadecimal characters is refused" \
  stable "$VERSION" "$URL" "$ART" "$WORK/signing.pem" "$SHORT_ANCHOR" 0.0.1
refuses "an empty anchor is refused, which is exactly what a public repository is handed for a secret scoped to private ones" \
  stable "$VERSION" "$URL" "$ART" "$WORK/signing.pem" "" 0.0.1
refuses "an artefact that is not there is refused rather than digested as nothing" \
  stable "$VERSION" "$URL" "$WORK/absent.zip" "$WORK/signing.pem" "$ANCHOR" 0.0.1
refuses "a minimumLauncher that does not begin with dotted integers is refused" \
  stable "$VERSION" "$URL" "$ART" "$WORK/signing.pem" "$ANCHOR" "latest"

echo
echo "$FAILURES failed"
[ "$FAILURES" -eq 0 ]
