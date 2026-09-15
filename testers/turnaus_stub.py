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

PIN = "483920"
TOKEN = "17|" + "z" * 40

lock = threading.Lock()

state = {
    "pin_spent": False,
    # The two columns this slice is about. `checked_at` is what a beat writes and
    # `heard_at` is what a dart writes, and the whole point is that a beat does not
    # touch the second one.
    "checked_at": None,
    "heard_at": None,
    "condition": None,
    "beats": 0,
    "round": [],            # the round in hand: list of sectors
    "counted": [],          # references already counted into it
    "detections_seen": 0,
    "visits": 0,
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

        if self.path == "/api/v1/autoscorer/devices":
            if body.get("pin") != PIN or state["pin_spent"]:
                record({"event": "pairing_refused", "pin_len": len(str(body.get("pin", "")))})
                return self.reply(422, {"message": "The given data was invalid.",
                                        "errors": {"pin": ["pin_not_redeemable"]}})
            state["pin_spent"] = True
            record({"event": "paired", "label": body.get("label"),
                    "token_sha256_16": hashlib.sha256(TOKEN.encode()).hexdigest()[:16]})
            return self.reply(201, {"data": {
                "token": TOKEN,
                "organisationId": 3,
                "device": {"id": 1, "label": body.get("label"),
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
            record({"event": "counted", "reference": reference, "sector": sector,
                    "auth_sha256_16": auth_digest, "round": list(state["round"])})
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
            with lock:
                state["beats"] += 1
                state["condition"] = condition
                # `checked_at` and ONLY `checked_at`. `heard_at` means darts have been
                # arriving; a beat moving it would collapse two columns into one.
                state["checked_at"] = time.time()
            record({"event": "beat", "condition": condition, "auth_sha256_16": auth_digest,
                    "round": list(state["round"]), "counted": list(state["counted"]),
                    "heard_at": state["heard_at"]})
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
