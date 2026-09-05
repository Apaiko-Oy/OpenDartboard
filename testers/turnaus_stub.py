#!/usr/bin/env python3
"""#822's measuring instrument, not a Turnaus.

It implements exactly the three addresses #820 and #821 define, with #821's dedup rule
-- `reference` absorbed while the round it belongs to is the round in hand -- so that a
retry can be shown to produce one dart rather than two. Everything that arrives is
appended to a JSONL transcript, credential included as a sha256 so the transcript can
prove which credential was presented without carrying it.

Modes, by environment variable:
  STUB_FAIL_FIRST=n   answer the first n detection pushes with 500, then behave
  STUB_PORT           default 8899
"""
import hashlib
import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(os.environ.get("STUB_PORT", "8899"))
FAIL_FIRST = int(os.environ.get("STUB_FAIL_FIRST", "0"))
TRANSCRIPT = os.environ.get("STUB_TRANSCRIPT", "/run822/transcript.jsonl")

PIN = "483920"
TOKEN = "17|" + "z" * 40

state = {
    "pin_spent": False,
    "round": [],            # the round in hand: list of sectors
    "counted": [],          # references already counted into it
    "detections_seen": 0,
    "visits": 0,
}


def record(event):
    event["at"] = time.time()
    with open(TRANSCRIPT, "a") as fh:
        fh.write(json.dumps(event) + "\n")
        fh.flush()


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

        if self.path == "/api/v1/autoscorer/takeouts":
            had = list(state["round"])
            state["round"] = []
            state["counted"] = []
            visit_id = None
            if had:
                state["visits"] += 1
                visit_id = "01K4G2ZC9J7QMB4Y2K7E1WX0P%d" % state["visits"]
            record({"event": "takeout", "closed": had, "visitId": visit_id,
                    "auth_sha256_16": auth_digest})
            return self.reply(202, {"data": {"visitId": visit_id}})

        record({"event": "unknown_path", "path": self.path})
        return self.reply(404, {"message": "Not Found"})


if __name__ == "__main__":
    open(TRANSCRIPT, "w").close()
    server = HTTPServer(("127.0.0.1", PORT), Handler)
    sys.stderr.write("stub listening on 127.0.0.1:%d\n" % PORT)
    sys.stderr.flush()
    server.serve_forever()
