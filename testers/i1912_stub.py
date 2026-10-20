#!/usr/bin/env python3
# turnaus#1912: the #1787 routes on a stub that can be slow, beside the dart doors.
#
# testers/turnaus_stub.py serves the push, takeout and beat doors and no #1787 route, and
# answers HTTP/1.0 -- one connection per request whatever the client asks for -- which is
# right for every tester that uses it and wrong for this one, which asks two things that
# stub cannot say: whether a dart is answered while a frames upload is in flight, and
# whether the board's posts share a connection. So this one is threaded (a request in
# flight does not hold the next), speaks HTTP/1.1 with a Content-Length on every answer
# (so keep-alive is possible), and records every request with its connection's identity.
#
# The frames answer is held FRAMES_HOLD_S seconds before it is answered (default 10):
# that is the 8..12 s upload of 2026-10-20 on the Pi's Wi-Fi, as the board sees it -- a
# request it has sent and is waiting on.
#
# Env: STUB_PORT (default 8912), STUB_TRANSCRIPT (JSONL, default /tmp/stub1912.jsonl),
# FRAMES_HOLD_S. Stdlib only. The first post of a T19 is answered 503 once, so the
# board's retry line has something to say.

import json
import os
import socketserver
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(os.environ.get("STUB_PORT", "8912"))
TRANSCRIPT = os.environ.get("STUB_TRANSCRIPT", "/tmp/stub1912.jsonl")
FRAMES_HOLD_S = float(os.environ.get("FRAMES_HOLD_S", "10"))

lock = threading.Lock()
state = {"frames_asked": 0, "connections": 0}


def record(event):
    event["t"] = time.time()
    with lock:
        with open(TRANSCRIPT, "a") as f:
            f.write(json.dumps(event, sort_keys=True) + "\n")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def setup(self):
        super().setup()
        with lock:
            state["connections"] += 1
            self.connection_no = state["connections"]

    def log_message(self, *args):
        pass

    def reply(self, status, payload):
        data = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
        self.wfile.flush()

    def body(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        raw = self.rfile.read(n) if n else b""
        try:
            return json.loads(raw or b"{}")
        except ValueError:
            return {}

    def do_GET(self):
        record({"event": "request", "method": "GET", "path": self.path, "connection": self.connection_no})
        if self.path == "/api/v1/autoscorer/frame-requests":
            with lock:
                state["frames_asked"] += 1
                first = state["frames_asked"] == 1
            return self.reply(200, {"data": [{"reference": "01JFRAMESREQUESTED00000001"}] if first else []})
        return self.reply(404, {"message": "no such route"})

    def do_POST(self):
        b = self.body()
        record({"event": "request", "method": "POST", "path": self.path, "connection": self.connection_no,
                "reference": b.get("reference"), "sector": b.get("sector"),
                "log_offset": b.get("offset"), "log_bytes": len(b.get("text", "") or ""),
                "log_has_marker": "MARKER-1912" in (b.get("text", "") or "")})
        p = self.path
        if p == "/api/v1/autoscorer/detections":
            # The first post of a T19 is answered 503, once: the retry line's needle.
            with lock:
                refuse = b.get("sector") == "T19" and not state.get("refused_t19")
                if refuse:
                    state["refused_t19"] = True
            if refuse:
                record({"event": "refused_503", "sector": "T19", "connection": self.connection_no})
                return self.reply(503, {"message": "unwell, once"})
            return self.reply(202, {"data": {"outcome": "COUNTED"}})
        if p == "/api/v1/autoscorer/takeouts":
            return self.reply(202, {"data": {"outcome": "TAKEOUT", "visitId": None}})
        if p == "/api/v1/autoscorer/heartbeats":
            return self.reply(200, {"data": {"condition": b.get("condition", ""),
                                             "intervalSeconds": 30, "silenceSeconds": 90}})
        if p == "/api/v1/autoscorer/log":
            return self.reply(202, {"data": {"length": (b.get("offset") or 0) + len(b.get("text", "") or "")}})
        if p.startswith("/api/v1/autoscorer/frame-requests/"):
            record({"event": "frames_hold_begun", "path": p, "connection": self.connection_no})
            time.sleep(FRAMES_HOLD_S)
            record({"event": "frames_hold_ended", "path": p, "connection": self.connection_no})
            return self.reply(202, {"data": {"taken": True}})
        return self.reply(404, {"message": "no such route"})


class Server(socketserver.ThreadingMixIn, HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


if __name__ == "__main__":
    open(TRANSCRIPT, "w").close()
    srv = Server(("127.0.0.1", PORT), Handler)
    print("stub1912 listening on %d" % PORT, flush=True)
    srv.serve_forever()
