#!/usr/bin/env bash
# #1408: mint the signed manifest a release ships (ADR-0077).
#
# WHY THIS IS A SCRIPT AND NOT TEN LINES OF YAML. A step that only ever runs on a `v*`
# tag is a step nobody can run, and the failure it would hide is the one this whole
# slice is about: an unsigned or malformed manifest attached to a real release is worse
# than no manifest at all. As a script it is called by .github/workflows/release.yml and
# by testers/i1408_sign_check.sh, which runs it against a throwaway key on any machine
# with openssl and checks its output the way the detector's own reader does.
#
# WHAT A MANIFEST IS (ADR-0077, "What a manifest says"):
#
#     {"payload": "<base64>", "signature": "<base64 ECDSA P-256 over the payload bytes>"}
#
#     {"channel":"stable","version":"0.1.4","url":"https://…zip",
#      "sha256":"<64 lowercase hex>","size":39845120,"minimumLauncher":"0.0.1"}
#
# THE SIGNATURE COVERS BYTES, NOT AN OBJECT. The payload is written once, signed as it
# stands and base64'd as it stands; nothing re-serialises it. So there is no
# canonicalisation rule for this signer and src/update/manifest.hpp to drift apart on,
# which ADR-0077 §2 names as the usual way a scheme like this fails quietly.
#
# THE READER IS THE AUTHORITY, and two of its rules are not obvious:
#
#   `version` is compared to the running build by DIFFERENCE (update_check.hpp), and
#   what a build calls itself is APP_VERSION -- the tag with its leading `v` removed,
#   which is what `--version` prints. So a manifest that says `v0.1.4` where the board
#   says `0.1.4` reads as "an update is available" for ever. This script therefore
#   refuses a version that starts with `v`, rather than accepting one and shipping the
#   loop. ADR-0077's example line writes `v1.4.2` and its own annotation beside it says
#   "exactly what --version prints on the artefact"; the annotation is the one the code
#   implements.
#
#   `minimumLauncher` IS ordered, in the one place this program orders versions
#   (apply_update.hpp), over leading dotted integers with a leading `v` and any
#   `-suffix` ignored. A launcher below the line refuses and a board stays where it is.
#
# --anchor IS REQUIRED, and that is the point of it. The anchor is what the artefact
# built from this same run has compiled in, so comparing the signing key's own public
# point against it here makes the failure that matters impossible to ship: a manifest
# no released artefact can verify. A mismatch is a refusal by name, before anything is
# written.
#
#   scripts/sign-manifest.sh \
#     --channel stable --version 0.1.4 \
#     --url https://github.com/Apaiko-Oy/OpenDartboard/releases/download/v0.1.4/opendartboard-0.1.4-windows-x64.zip \
#     --artefact dist/opendartboard-0.1.4-windows-x64.zip \
#     --key "$RUNNER_TEMP/signing.pem" --anchor "$OD_UPDATE_ANCHOR_CURRENT" \
#     --minimum-launcher 0.0.1 --out dist/manifest.json
set -euo pipefail

channel=""
version=""
url=""
artefact=""
key=""
anchor=""
minimum_launcher=""
out=""

die() {
  echo "sign-manifest: $1" >&2
  exit 1
}

while [ $# -gt 0 ]; do
  case "$1" in
    --channel) channel="${2:-}"; shift 2 ;;
    --version) version="${2:-}"; shift 2 ;;
    --url) url="${2:-}"; shift 2 ;;
    --artefact) artefact="${2:-}"; shift 2 ;;
    --key) key="${2:-}"; shift 2 ;;
    --anchor) anchor="${2:-}"; shift 2 ;;
    --minimum-launcher) minimum_launcher="${2:-}"; shift 2 ;;
    --out) out="${2:-}"; shift 2 ;;
    *) die "unknown argument '$1'" ;;
  esac
done

for pair in "channel:$channel" "version:$version" "url:$url" "artefact:$artefact" \
  "key:$key" "anchor:$anchor" "minimum-launcher:$minimum_launcher" "out:$out"; do
  [ -n "${pair#*:}" ] || die "--${pair%%:*} was not given, and every one of them is required"
done

command -v openssl > /dev/null || die "openssl is not on this machine, and it is what signs"

# ---- what may be said, checked before anything is written -------------------------------
# A manifest is read by two programs that will not tell you what they disliked about it
# until a board is in a pub, so everything they refuse is refused here instead, by name.
case "$channel" in
  stable | beta) ;;
  *) die "the channel is '$channel'; src/update/update_channel.hpp knows stable and beta" ;;
esac

case "$version" in
  v* | V*) die "the version is '$version'; a manifest states what --version prints, which has no leading v" ;;
esac
printf '%s' "$version" | grep -Eq '^[0-9A-Za-z][0-9A-Za-z.+-]*$' \
  || die "the version '$version' is not something a board could print back"

case "$url" in
  https://*) ;;
  *) die "the url is not https, and a board may not be sent to fetch a release in the clear (ADR-0077 §6)" ;;
esac
case "$url" in
  *[\"\\]*) die "the url carries a quote or a backslash and would not survive being written into JSON" ;;
esac

printf '%s' "$minimum_launcher" | grep -Eq '^v?[0-9]+(\.[0-9]+)*([.+-][0-9A-Za-z.+-]*)?$' \
  || die "minimumLauncher '$minimum_launcher' does not begin with dotted integers, so apply_update.hpp would refuse every launcher"

[ -f "$artefact" ] || die "there is no artefact at $artefact, so there is nothing to state the digest of"
[ -f "$key" ] || die "there is no signing key at $key"

printf '%s' "$anchor" | grep -Eq '^[0-9a-fA-F]{128}$' \
  || die "the anchor is not 128 hexadecimal characters (it is ${#anchor} characters), so no build could hold it -- see src/update/update_keys.hpp"

# ---- the key: the right curve, and the one this release's artefact trusts ----------------
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

openssl ec -in "$key" -pubout -out "$work/public.pem" > /dev/null 2>&1 \
  || die "openssl will not read a private EC key at $key"

openssl ec -pubin -in "$work/public.pem" -text -noout > "$work/public.txt" 2>/dev/null \
  || die "openssl will not describe the public half of $key"

grep -Eq 'prime256v1|P-256' "$work/public.txt" \
  || die "the signing key is not P-256, and ADR-0077 §2 verifies through CNG's BCRYPT_ECDSA_P256 and nothing else"

# The uncompressed point, 04 then X then Y -- update_keys.hpp's own recipe, and an anchor
# is the same 128 characters with the 04 dropped.
point="$(
  awk '/pub:/{reading=1; next} reading{ if ($0 !~ /:/ || $0 ~ /ASN1|NIST/) exit; gsub(/[^0-9a-f]/,""); printf "%s", $0 }' "$work/public.txt"
)"
[ "${#point}" -eq 130 ] || die "openssl did not print an uncompressed public point (${#point} characters)"
case "$point" in
  04*) ;;
  *) die "the public point does not begin 04, so it is not uncompressed" ;;
esac

signing_anchor="${point#04}"
given_anchor="$(printf '%s' "$anchor" | tr 'A-F' 'a-f')"
[ "$signing_anchor" = "$given_anchor" ] || die "the signing key's public half is not the anchor this release's artefact was built with, so nothing this key signs could ever be accepted by it"

# ---- the payload, written once and signed as it stands ------------------------------------
digest="$(openssl dgst -sha256 -r "$artefact" | cut -d' ' -f1 | tr 'A-F' 'a-f')"
printf '%s' "$digest" | grep -Eq '^[0-9a-f]{64}$' || die "openssl did not print a sha256 of $artefact"
size="$(wc -c < "$artefact" | tr -d '[:space:]')"
[ "$size" -gt 0 ] || die "$artefact is empty, and a download with no length cannot be refused before it is made"

printf '{"channel":"%s","version":"%s","url":"%s","sha256":"%s","size":%s,"minimumLauncher":"%s"}' \
  "$channel" "$version" "$url" "$digest" "$size" "$minimum_launcher" > "$work/payload.json"

openssl dgst -sha256 -sign "$key" -out "$work/signature.der" "$work/payload.json" \
  || die "openssl would not sign the payload"

# openssl writes DER: SEQUENCE { INTEGER r, INTEGER s }, which is the shape
# manifest.hpp::derSignature reads and PHP's openssl_verify reads. Said out loud here so
# that the day somebody reaches for a raw r||s signer, this line is what refuses it.
openssl asn1parse -inform DER -in "$work/signature.der" 2>/dev/null | head -1 | grep -q SEQUENCE \
  || die "the signature is not a DER SEQUENCE, and both readers of a manifest parse DER"

# The script's own control: the bytes that are about to be published verify under the
# public half of the key that signed them. This cannot catch a wrong key -- the anchor
# comparison above is what does that -- it catches a payload that moved between signing
# and writing, which is the one way a script like this silently ships something broken.
openssl dgst -sha256 -verify "$work/public.pem" -signature "$work/signature.der" "$work/payload.json" > /dev/null \
  || die "the signature this run just made does not verify over the payload this run just wrote"

payload_b64="$(openssl base64 -A -in "$work/payload.json")"
signature_b64="$(openssl base64 -A -in "$work/signature.der")"

mkdir -p "$(dirname "$out")"
printf '{\n  "payload": "%s",\n  "signature": "%s"\n}\n' "$payload_b64" "$signature_b64" > "$out"

echo "sign-manifest: $out"
echo "  channel:         $channel"
echo "  version:         $version"
echo "  url:             $url"
echo "  sha256:          $digest"
echo "  size:            $size"
echo "  minimumLauncher: $minimum_launcher"
echo "  anchor:          ${signing_anchor:0:16}… (the signing key's own public point)"
