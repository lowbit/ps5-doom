"""Sends files to DOOM's Send games screen, the way its web page does, and prints what the console did.

    uv run --no-project python tools/send.py http://192.168.0.90:9666/ FILE...

The screen must be open on the console (Add games, then Send from PC or phone); it shows the address.
Used with test/console-send.cfg for hands-free tests.
"""
import argparse
import http.client
import json
import time
import urllib.parse
from pathlib import Path


def request(host, port, method, path, body=None):
    conn = http.client.HTTPConnection(host, port, timeout=120)
    conn.request(method, path, body=body)
    response = conn.getresponse()
    reply = response.read()
    conn.close()
    return response.status, reply.decode(errors="replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("address", help="the address the screen shows")
    ap.add_argument("files", nargs="+", type=Path)
    ap.add_argument("--wait", type=int, default=60, help="seconds to wait for the screen to answer")
    args = ap.parse_args()
    url = urllib.parse.urlsplit(args.address)

    deadline = time.time() + args.wait
    while True:
        try:
            request(url.hostname, url.port, "GET", "/")
            break
        except OSError:
            if time.time() > deadline:
                raise SystemExit(f"nothing answers at {args.address}")
            time.sleep(1)

    for path in args.files:
        data = path.read_bytes()
        started = time.time()
        status, reply = request(url.hostname, url.port, "PUT", "/upload/" + urllib.parse.quote(path.name), data)
        seconds = max(time.time() - started, 0.001)
        print(f"{path.name}: {status} {reply} ({len(data) / 1048576:.1f} MB, {len(data) / 1048576 / seconds:.1f} MB/s)")

    # The console reports what it did with each file once it has read it.
    last, quiet = 0, 0
    while quiet < 5:
        notes = json.loads(request(url.hostname, url.port, "GET", f"/status?after={last}")[1])["notes"]
        for note in notes:
            print(("done: " if note["ok"] else "failed: ") + note["text"])
            last = note["id"]
        quiet = 0 if notes else quiet + 1
        time.sleep(1)


if __name__ == "__main__":
    main()
