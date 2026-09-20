#!/usr/bin/env python3
"""#822's measuring instrument, not a Turnaus.

It implements exactly the three addresses #820 and #821 define, with #821's dedup rule
-- `reference` absorbed while the round it belongs to is the round in hand -- so that a
retry can be shown to produce one dart rather than two. Everything that arrives is
appended to a JSONL transcript, credential included as a sha256 so the transcript can
prove which credential was presented without carrying it.

#892 extends it with the fourth address and with a model of the one server-side rule
this slice is about. `AutoscorerCondition::asOf()` answers `Condition::Unknown` past
`silenceSeconds()` since `checked_at` whatever the row says, and `hasGoneSilentBy()` is
`checked_at is null or (now - checked_at) > silence`. That rule is reimplemented here, to
the comparison, and sampled once a second into the transcript -- so a run produces a time
series of what a Station's screen would have been drawing. It is a MODEL of Turnaus and
not a Turnaus: #850's own tests are what prove the server half.

Modes, by environment variable:
  STUB_FAIL_FIRST=n   answer the first n detection pushes with 500, then behave
  STUB_PORT           default 8899
  STUB_INTERVAL_SECONDS   what the beat answer names as intervalSeconds (default 15)
  STUB_SILENCE_SECONDS    what it names as silenceSeconds, and the window the model
                          applies (default 60)
  STUB_SAMPLE_SECONDS     how often to sample the model (default 1; 0 turns it off)

#1259 adds what a board asking for a code at the console meets:
  STUB_CLUB_CODES         comma-separated club codes, each single use (default 483920). Every
                          redemption issues a NEW club token and only the newest unrevoked one is
                          admitted, so a re-paired board is provably using its second credential.
  STUB_CASUAL_CODES       the same for the Casual door (default 571643); a redemption also
                          un-ends the evening, because it is a new binding.
  STUB_REVOKE_CLUB_AFTER_BEATS  revoke the current club token after this many club beats made
                          with it (0: never) -- #1246's revocation, met as #822's 401.
  STUB_RATE_LIMIT_FIRST   answer the first n pairing requests, at either door, with 429 and
                          `Retry-After: STUB_RETRY_AFTER` (default 37), as `api-pairing` does.
"""
import hashlib
import json
import os
import sys
import time
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(os.environ.get("STUB_PORT", "8899"))
FAIL_FIRST = int(os.environ.get("STUB_FAIL_FIRST", "0"))
INTERVAL_SECONDS = int(os.environ.get("STUB_INTERVAL_SECONDS", "15"))
SILENCE_SECONDS = int(os.environ.get("STUB_SILENCE_SECONDS", "60"))
SAMPLE_SECONDS = float(os.environ.get("STUB_SAMPLE_SECONDS", "1"))

# The five words `Condition::selfReportable()` holds. UNKNOWN and OFFLINE are refused
# with 422 by the real controller and are refused here: the first is the conclusion the
# silence window draws for itself, the second is a claim a board cannot make over a
# working connection (ADR-0065).
SELF_REPORTABLE = ["UPDATING", "INITIALISING", "CALIBRATING", "READY", "ERROR"]
TRANSCRIPT = os.environ.get("STUB_TRANSCRIPT", "/run822/transcript.jsonl")

# ---------------------------------------------------------------------------------------
# #1474 / #1343: what a beat may say about the board's cameras.
#
# `App\Autoscoring\CameraReport` ported, to the comparison, because the whole of #1474 is
# whether the body a real board sends is one the real server accepts -- and a stub that
# merely wrote down whatever arrived would pass on a body Turnaus answers 422 to. The
# three behaviours that matter, and they are three different answers rather than degrees
# of one:
#
#   ABSENT      no `cameras` key. 200, recorded as unknown. Every board from before
#               #1474, which is the fleet mid-upgrade, and it must go on beating.
#   REFUSED     the key is there and malformed -- a member missing, a member that is not
#               a whole number, fitted outside 1..16, or a FOURTH member. 422, and the
#               club loses the condition as well as the count, which is why the detector
#               must never be able to send one.
#   DROPPED     every member passes its own rule and the three disagree: scoring + dark
#               above fitted is arithmetic no board can be in. 200 -- the beat is worth
#               more than the census -- and the count is recorded as unknown.
#
# `array:fitted,scoring,dark` is Laravel's closed-object rule and is the sharp edge here:
# a fourth member is a 422, so a detector that ever adds the machine's serial to this
# object takes the board's condition off the club's page with it.
MOST_A_BOARD_HAS = 16


def camera_report(body):
    """(status, report_or_None, why). 422 status means the whole beat is refused."""
    if "cameras" not in body:
        return 200, None, "absent"

    cameras = body["cameras"]
    if not isinstance(cameras, dict):
        return 422, None, "not an object"

    members = ("fitted", "scoring", "dark")
    extra = sorted(set(cameras) - set(members))
    if extra:
        return 422, None, "members this object does not have: " + ",".join(extra)

    for name in members:
        if name not in cameras:
            return 422, None, "no " + name
        value = cameras[name]
        # bool is an int in Python and is not a whole number here.
        if isinstance(value, bool) or not isinstance(value, int):
            return 422, None, name + " is not a whole number"
        if value < 0 or value > MOST_A_BOARD_HAS:
            return 422, None, name + " out of 0.." + str(MOST_A_BOARD_HAS)

    if cameras["fitted"] < 1:
        return 422, None, "fitted below 1"

    report = {n: cameras[n] for n in members}
    if report["scoring"] + report["dark"] > report["fitted"]:
        return 200, None, "arithmetic no board can be in"

    return 200, report, "stated"


PIN = "483920"
TOKEN = "17|" + "z" * 40
# #1259: codes and tokens by generation. The first club token is TOKEN, unchanged, so every
# earlier harness reads the same transcript it always did.
CLUB_CODES = [c for c in os.environ.get("STUB_CLUB_CODES", PIN).split(",") if c]
REVOKE_CLUB_AFTER_BEATS = int(os.environ.get("STUB_REVOKE_CLUB_AFTER_BEATS", "0"))
RATE_LIMIT_FIRST = int(os.environ.get("STUB_RATE_LIMIT_FIRST", "0"))
RETRY_AFTER = os.environ.get("STUB_RETRY_AFTER", "37")


def club_token(generation):
    return TOKEN if generation == 1 else "%d|" % (16 + generation) + "z" * 40


def digest(token):
    return hashlib.sha256(token.encode()).hexdigest()[:16]

# #891: the third door. A different six-digit code, a different table, a different
# credential, and a binding to one Casual Contest rather than to an Organisation (#887,
# ADR-0069). The two doors do not read each other's codes and the two credentials are
# disjoint, which is modelled here rather than assumed: a token presented at the wrong
# door is a 401.
CASUAL_PIN = "571643"
CASUAL_TOKEN = "31|" + "y" * 40
CASUAL_CODES = [c for c in os.environ.get("STUB_CASUAL_CODES", CASUAL_PIN).split(",") if c]
CASUAL_CONTEST_ID = 12
CASUAL_BOARD_ID = 4
# After this many casual detections the Contest is Given Up. #887's release() DELETES the
# token, so what a board meets is the guard refusing it -- not a special answer saying the
# evening ended, which would be exactly the oracle #887 collapses its refusals to deny.
# 0 means the evening never ends.
GIVE_UP_AFTER = int(os.environ.get("STUB_GIVE_UP_AFTER", "0"))
# The same ending reached without a dart, because the beat is what usually meets it first:
# it goes every interval and a dart goes only when somebody throws.
GIVE_UP_AFTER_BEATS = int(os.environ.get("STUB_GIVE_UP_AFTER_BEATS", "0"))
# Whether this deployment serves a heartbeat address for a Casual board at all.
CASUAL_BEAT = os.environ.get("STUB_CASUAL_BEAT", "1") == "1"

lock = threading.Lock()

state = {
    "pin_spent": False,
    # #1259
    "club_spent": [],
    "club_generation": 0,
    "club_token": None,       # the one club token admitted now; None before pairing or once revoked
    "club_token_beats": 0,
    "casual_spent": [],
    "pairing_requests": 0,
    # The two columns this slice is about. `checked_at` is what a beat writes and
    # `heard_at` is what a dart writes, and the whole point is that a beat does not
    # touch the second one.
    "checked_at": None,
    "heard_at": None,
    "condition": None,
    "beats": 0,
    # #1474: the last camera census this board stated, or None for a board that has
    # never said. Null is *nobody can say* and is never nought.
    "cameras": None,
    "round": [],            # the round in hand: list of sectors
    "counted": [],          # references already counted into it
    "detections_seen": 0,
    "visits": 0,
    # #891: the Casual Contest's own half. A separate round in hand, because a board's
    # darts land in the evening it is bound to and in nothing else, and a separate
    # given_up flag, because the evening ends.
    "casual_pin_spent": False,
    "casual_round": [],
    "casual_counted": [],
    "casual_detections_seen": 0,
    "casual_given_up": False,
    "casual_checked_at": None,
    "casual_condition": None,
}


write_lock = threading.Lock()


def record(event):
    event["at"] = time.time()
    with write_lock:
        with open(TRANSCRIPT, "a") as fh:
            fh.write(json.dumps(event) + "\n")
            fh.flush()


def as_of(now):
    """`AutoscorerCondition::asOf()`, to the comparison.

    Past `silenceSeconds` since `checked_at` it answers UNKNOWN whatever the row holds,
    because nobody has been watching and *nothing is known* is the only true answer. A
    Station drawing UNKNOWN degrades to the keypad.
    """
    checked = state["checked_at"]
    if checked is None or (now - checked) > SILENCE_SECONDS:
        return "UNKNOWN"
    return state["condition"] or "UNKNOWN"


def sample_forever():
    while True:
        now = time.time()
        checked = state["checked_at"]
        record({"event": "state_sample",
                "as_of": as_of(now),
                "silent": as_of(now) == "UNKNOWN",
                "since_checked_s": None if checked is None else round(now - checked, 2),
                "since_heard_s": None if state["heard_at"] is None else round(now - state["heard_at"], 2),
                "round": list(state["round"])})
        time.sleep(SAMPLE_SECONDS)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, status, payload):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length).decode() if length else ""
        try:
            body = json.loads(raw) if raw else {}
        except Exception:
            body = {}

        auth = self.headers.get("Authorization", "")
        presented = auth[7:] if auth.startswith("Bearer ") else ""
        auth_digest = hashlib.sha256(presented.encode()).hexdigest()[:16] if presented else None

        # ------------------------------------------------------------------
        # #891: the Casual half. Everything under /api/v1/casual/ is bound to one Contest
        # and is refused outright once that Contest has been given up, because #887
        # deletes the token at that moment and there is nothing left for the guard to
        # admit. A board meets a 401 and not an explanation.
        # ------------------------------------------------------------------
        if self.path in ("/api/v1/casual/boards", "/api/v1/autoscorer/devices"):
            state["pairing_requests"] += 1
            if state["pairing_requests"] <= RATE_LIMIT_FIRST:
                record({"event": "pairing_rate_limited", "path": self.path, "retry_after": RETRY_AFTER})
                body_bytes = json.dumps({"message": "Too Many Attempts."}).encode()
                self.send_response(429)
                self.send_header("Content-Type", "application/json")
                self.send_header("Retry-After", RETRY_AFTER)
                self.send_header("Content-Length", str(len(body_bytes)))
                self.end_headers()
                self.wfile.write(body_bytes)
                return

        if self.path == "/api/v1/casual/boards":
            pin = body.get("pin")
            if pin not in CASUAL_CODES or pin in state["casual_spent"]:
                # One refusal, four causes: never minted, already spent, expired, and the
                # evening ended. Telling them apart hands a guesser an oracle.
                record({"event": "casual_pairing_refused", "pin_len": len(str(body.get("pin", "")))})
                return self.reply(422, {"message": "The given data was invalid.",
                                        "errors": {"pin": ["casual_board.error.code_not_redeemable"]}})
            state["casual_pin_spent"] = True
            state["casual_spent"].append(pin)
            state["casual_given_up"] = False
            record({"event": "casual_paired", "label": body.get("label"),
                    "token_sha256_16": hashlib.sha256(CASUAL_TOKEN.encode()).hexdigest()[:16]})
            return self.reply(201, {"data": {
                "token": CASUAL_TOKEN,
                "casualContestId": CASUAL_CONTEST_ID,
                "board": {"id": CASUAL_BOARD_ID, "label": body.get("label"),
                          "pairedAt": "2026-09-06T19:31:00+00:00"}}})

        if self.path.startswith("/api/v1/casual/"):
            # The guard, and it is the whole of the give-up story. A deleted token cannot
            # be admitted, and a board holding the club's credential is not this board.
            if presented != CASUAL_TOKEN or state["casual_given_up"]:
                record({"event": "casual_refused", "path": self.path,
                        "auth_sha256_16": auth_digest,
                        "given_up": state["casual_given_up"]})
                return self.reply(401, {"message": "Unauthenticated."})

            if self.path == "/api/v1/casual/detections":
                state["casual_detections_seen"] += 1
                reference = body.get("reference", "")
                sector = body.get("sector", "")
                if reference in state["casual_counted"]:
                    record({"event": "casual_absorbed", "reference": reference, "sector": sector,
                            "auth_sha256_16": auth_digest, "round": list(state["casual_round"])})
                    return self.reply(202, {"data": {"outcome": "ABSORBED",
                                                     "casualContestId": CASUAL_CONTEST_ID,
                                                     "round": list(state["casual_round"])}})
                state["casual_round"].append(sector)
                state["casual_counted"].append(reference)
                state["casual_checked_at"] = time.time()
                record({"event": "casual_counted", "reference": reference, "sector": sector,
                        "body": body, "auth_sha256_16": auth_digest,
                        "round": list(state["casual_round"])})
                if GIVE_UP_AFTER and state["casual_detections_seen"] >= GIVE_UP_AFTER:
                    # The person at the screen gave the evening up. #887 releases every
                    # board still on it: the token is deleted and the round in hand goes
                    # with it, because a round that never reached a takeout is a record of
                    # nothing (ADR-0069).
                    state["casual_given_up"] = True
                    state["casual_round"] = []
                    state["casual_counted"] = []
                    record({"event": "casual_given_up",
                            "after_detections": state["casual_detections_seen"]})
                return self.reply(202, {"data": {"outcome": "COUNTED",
                                                 "casualContestId": CASUAL_CONTEST_ID,
                                                 "round": list(state["casual_round"])}})

            if self.path == "/api/v1/casual/takeouts":
                had = list(state["casual_round"])
                state["casual_round"] = []
                state["casual_counted"] = []
                state["casual_checked_at"] = time.time()
                # NOTHING IS WRITTEN HERE, and that is #888's whole point: the board
                # reports and the browser writes. No visitId comes back because no Visit
                # was appended by this push.
                record({"event": "casual_takeout", "closed": had, "auth_sha256_16": auth_digest})
                return self.reply(202, {"data": {"casualContestId": CASUAL_CONTEST_ID,
                                                 "round": []}})

            if self.path == "/api/v1/casual/heartbeats":
                if not CASUAL_BEAT:
                    record({"event": "casual_beat_unserved", "condition": body.get("condition", "")})
                    return self.reply(404, {"message": "Not Found"})
                condition = body.get("condition", "")
                if condition not in SELF_REPORTABLE:
                    record({"event": "casual_beat_refused", "condition": condition})
                    return self.reply(422, {"message": "The given data was invalid.",
                                            "errors": {"condition": ["The selected condition is invalid."]}})
                with lock:
                    state["casual_condition"] = condition
                    state["casual_checked_at"] = time.time()
                    state["casual_beats"] = state.get("casual_beats", 0) + 1
                    beats_now = state["casual_beats"]
                record({"event": "casual_beat", "condition": condition, "auth_sha256_16": auth_digest,
                        "cameras": camera_report(body)[1], "cameras_raw": body.get("cameras"),
                        "round": list(state["casual_round"])})
                if GIVE_UP_AFTER_BEATS and beats_now >= GIVE_UP_AFTER_BEATS:
                    state["casual_given_up"] = True
                    state["casual_round"] = []
                    state["casual_counted"] = []
                    record({"event": "casual_given_up", "after_beats": beats_now})
                return self.reply(200, {"data": {"condition": condition,
                                                 "intervalSeconds": INTERVAL_SECONDS,
                                                 "silenceSeconds": SILENCE_SECONDS}})

            record({"event": "unknown_casual_path", "path": self.path})
            return self.reply(404, {"message": "Not Found"})

        # A club credential is the only thing the club's doors admit, and a Casual board's
        # is refused there. Disjoint tokenables, modelled: RequireAutoscorerDevice will not
        # have a CasualContestBoard at any price (#887).
        if self.path.startswith("/api/v1/autoscorer/") and self.path != "/api/v1/autoscorer/devices":
            if presented != state["club_token"]:
                record({"event": "autoscorer_refused", "path": self.path, "auth_sha256_16": auth_digest})
                return self.reply(401, {"message": "Unauthenticated."})

        if self.path == "/api/v1/autoscorer/devices":
            pin = body.get("pin")
            if pin not in CLUB_CODES or pin in state["club_spent"]:
                record({"event": "pairing_refused", "pin_len": len(str(body.get("pin", "")))})
                return self.reply(422, {"message": "The given data was invalid.",
                                        "errors": {"pin": ["pin_not_redeemable"]}})
            state["pin_spent"] = True
            state["club_spent"].append(pin)
            state["club_generation"] += 1
            state["club_token"] = club_token(state["club_generation"])
            state["club_token_beats"] = 0
            record({"event": "paired", "label": body.get("label"), "device_id": state["club_generation"],
                    "accept": self.headers.get("Accept"),
                    "token_sha256_16": digest(state["club_token"])})
            return self.reply(201, {"data": {
                "token": state["club_token"],
                "organisationId": 3,
                "device": {"id": state["club_generation"], "label": body.get("label"),
                           "pairedAt": "2026-09-05T19:31:00+00:00"}}})

        if self.path == "/api/v1/autoscorer/detections":
            state["detections_seen"] += 1
            reference = body.get("reference", "")
            sector = body.get("sector", "")
            if reference in state["counted"]:
                record({"event": "absorbed", "reference": reference, "sector": sector,
                        "auth_sha256_16": auth_digest, "round": list(state["round"])})
                return self.reply(202, {"data": {"outcome": "ABSORBED", "round": {
                    "leg_id": 12, "ordinal": 1, "player_id": 44,
                    "darts": [{"throw_index": i, "sector": s, "score": 0,
                               "bounced_out": False, "suggestions": []}
                              for i, s in enumerate(state["round"])]}}})
            state["round"].append(sector)
            state["counted"].append(reference)
            # A dart is what `heard_at` means, and it moves `checked_at` too -- the
            # arrival of anything from this board is a check on it.
            state["heard_at"] = time.time()
            state["checked_at"] = time.time()
            # #1366: the BODY as it arrived, so a transcript can be asked what a push
            # really carried and not only what this stub chose to name. A position is a
            # pair of optional keys, and both their presence and their absence are the
            # assertion -- neither is readable from a field this stub picked out.
            record({"event": "counted", "reference": reference, "sector": sector,
                    "body": body, "auth_sha256_16": auth_digest, "round": list(state["round"])})
            if state["detections_seen"] <= FAIL_FIRST:
                # The dart is counted and then the answer is thrown away, which is the
                # exact failure ABSORBED exists for: the pub never learns it got through.
                record({"event": "answer_lost", "reference": reference, "sector": sector})
                return self.reply(500, {"message": "Server Error"})
            return self.reply(202, {"data": {"outcome": "COUNTED", "round": {
                "leg_id": 12, "ordinal": 1, "player_id": 44,
                "darts": [{"throw_index": i, "sector": s, "score": 0,
                           "bounced_out": False, "suggestions": []}
                          for i, s in enumerate(state["round"])]}}})

        if self.path == "/api/v1/autoscorer/heartbeats":
            condition = body.get("condition", "")
            if condition not in SELF_REPORTABLE:
                record({"event": "beat_refused", "condition": condition,
                        "auth_sha256_16": auth_digest})
                return self.reply(422, {"message": "The given data was invalid.",
                                        "errors": {"condition": ["The selected condition is invalid."]}})
            # #1474: asked BEFORE anything is written, as the real controller asks it --
            # a refused body records no condition at all.
            cam_status, cam_report, cam_why = camera_report(body)
            if cam_status == 422:
                record({"event": "beat_refused", "condition": condition, "cameras_why": cam_why,
                        "cameras_raw": body.get("cameras"), "auth_sha256_16": auth_digest})
                return self.reply(422, {"message": "The given data was invalid.",
                                        "errors": {"cameras": [cam_why]}})
            with lock:
                state["cameras"] = cam_report
                state["beats"] += 1
                state["club_token_beats"] += 1
                token_beats = state["club_token_beats"]
                state["condition"] = condition
                # `checked_at` and ONLY `checked_at`. `heard_at` means darts have been
                # arriving; a beat moving it would collapse two columns into one.
                state["checked_at"] = time.time()
            record({"event": "beat", "condition": condition, "auth_sha256_16": auth_digest,
                    "round": list(state["round"]), "counted": list(state["counted"]),
                    "heard_at": state["heard_at"],
                    # #1474: what the page would draw. `cameras` is the report as STORED --
                    # null is *nobody can say* and is never nought -- and `cameras_raw` is
                    # what really came over the wire, so a tester can prove the shape as
                    # well as the numbers.
                    "cameras": cam_report, "cameras_why": cam_why,
                    "cameras_raw": body.get("cameras")})
            if REVOKE_CLUB_AFTER_BEATS and token_beats >= REVOKE_CLUB_AFTER_BEATS:
                # #1246's revocation: the token row is gone, and the next request meets 401.
                record({"event": "club_revoked", "generation": state["club_generation"],
                        "after_beats": token_beats, "token_sha256_16": digest(state["club_token"])})
                state["club_token"] = None
            return self.reply(200, {"data": {"condition": condition,
                                             "intervalSeconds": INTERVAL_SECONDS,
                                             "silenceSeconds": SILENCE_SECONDS}})

        if self.path == "/api/v1/autoscorer/takeouts":
            had = list(state["round"])
            state["round"] = []
            state["counted"] = []
            visit_id = None
            if had:
                state["visits"] += 1
                visit_id = "01K4G2ZC9J7QMB4Y2K7E1WX0P%d" % state["visits"]
            state["heard_at"] = time.time()
            state["checked_at"] = time.time()
            record({"event": "takeout", "closed": had, "visitId": visit_id,
                    "auth_sha256_16": auth_digest})
            return self.reply(202, {"data": {"visitId": visit_id}})

        record({"event": "unknown_path", "path": self.path})
        return self.reply(404, {"message": "Not Found"})


if __name__ == "__main__":
    open(TRANSCRIPT, "w").close()
    if SAMPLE_SECONDS > 0:
        threading.Thread(target=sample_forever, daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    sys.stderr.write("stub listening on 127.0.0.1:%d interval=%ds silence=%ds\n"
                     % (PORT, INTERVAL_SECONDS, SILENCE_SECONDS))
    sys.stderr.flush()
    server.serve_forever()
