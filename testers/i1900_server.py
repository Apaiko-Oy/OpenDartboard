"""turnaus#1900: an artefact host that answers each path from a script in its own name.

  i1900_server.py <port> <good bytes file> <access log>

A request for .../<name>.tar.gz is answered by the scenario the name spells: `r` then
dash-separated answers, the n-th request getting the n-th answer and every later one the
last. An answer is a status (`403`), a status with a Retry-After (`429ra7`), or `bad` -- a
200 whose bytes are the right length and the wrong content. `200` serves the good bytes.
Any other token (`wall`) is a label and ignored. ONE LINE PER REQUEST goes to the access
log, `GET <path>`, before it is answered: the log is what the check counts.
"""
import http.server
import re
import sys

port, good_path, log_path = int(sys.argv[1]), sys.argv[2], sys.argv[3]
good = open(good_path, "rb").read()
seen = {}
ANSWER = re.compile(r"^(?:(\d{3})(?:ra(\d+))?|bad)$")


class Scripted(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        with open(log_path, "a") as log:
            log.write("GET %s\n" % self.path)
        name = self.path.rsplit("/", 1)[-1].split(".", 1)[0]
        plan = [t for t in name[1:].split("-") if ANSWER.match(t)] or ["404"]
        n = seen.get(self.path, 0)
        seen[self.path] = n + 1
        answer = plan[min(n, len(plan) - 1)]
        if answer == "bad":
            status, retry_after, body = 200, None, bytes(b ^ 0x5A for b in good)
        else:
            m = ANSWER.match(answer)
            status, retry_after = int(m.group(1)), m.group(2)
            body = good if status == 200 else b"refused by the i1900 script\n"
        self.send_response(status)
        if retry_after is not None:
            self.send_header("Retry-After", retry_after)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


http.server.ThreadingHTTPServer(("127.0.0.1", port), Scripted).serve_forever()
