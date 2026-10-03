"""Runs the deployed title on the PS5 with a test plan and collects what happened.

    uv run --no-project --with pillow python tools/console_test.py test/console-play.cfg [--timeout S]

Writes the plan to the title folder as test.cfg (plus a capture port), launches PPSA99666 with
the doomlaunch payload (tools/launcher, loaded through the Payload Manager), pulls the frames the
plan captures from the running title over TCP, converts them to PNG under build/test/run/,
prints the title's log lines from the kernel log (klogsrv capture in build/test/klog.txt) and
removes test.cfg again. The PS5Upload helper must be running.
"""
import argparse
import base64
import socket
import struct
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / "tools"))
import ps5  # noqa: E402
import push_ffpkg as push  # noqa: E402

TITLE = "PPSA99666"
PLDMGR = f"http://{ps5.PS5_IP}:8084"
LAUNCHER = ROOT / "build" / "launcher" / "doomlaunch.elf"
LAUNCHER_DIR = "/data/pldmgr/payloads/doomlaunch"
TEST_PLAN = f"/data/homebrew/{TITLE}/test.cfg"
CAPTURE_PORT = 9119
KLOG = ROOT / "build" / "test" / "klog.txt"
OUT = ROOT / "build" / "test" / "run"
FRAME = (1920, 1080)
NOISE = ("PFAuth", "FMEM", "PSM.UI", "catalog", "SHELLFLAG", "LOGIN MGR", "CloudMessaging",
         "ResArbitrator", "PlayGo", "AsyncStorage", "a53mm", "BAPM", "<118>", "[KERNEL]", "###",
         "num clients", "Amm config", "wt_application", "appmgr_create")


def get(url, timeout=10):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.read().decode(errors="replace")


def engine():
    push.LOG = ROOT / "build" / "deploy.log"
    push.start_engine()


def write_remote(path, data):
    r = push.http("POST", "/api/ps5/fs/write-bytes",
                  {"addr": push.XFER, "path": path, "bytes_b64": base64.b64encode(data).decode()})
    if "http_error" in r:
        sys.exit(f"writing {path} failed: {r}")


def install_launcher():
    data = LAUNCHER.read_bytes()
    try:
        if any(e["name"] == LAUNCHER.name and e["size"] == len(data)
               for e in ps5.list_dir(LAUNCHER_DIR)["entries"]):
            return
    except Exception:
        pass
    push.http("POST", "/api/ps5/fs/mkdir", {"addr": push.XFER, "path": LAUNCHER_DIR})
    write_remote(f"{LAUNCHER_DIR}/{LAUNCHER.name}", data)


def receive_exactly(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(min(1 << 20, size - len(data)))
        if not chunk:
            raise ConnectionError("capture stream ended early")
        data += chunk
    return bytes(data)


def tile_offset(x, y):
    bit = lambda v, n: (v >> n) & 1
    return (bit(x, 0) | bit(x, 1) << 1 | bit(y, 0) << 2 | bit(y, 1) << 3 | bit(y, 2) << 4 |
            bit(x, 2) << 5 | (bit(x, 3) ^ bit(y, 3)) << 6 | (bit(x, 4) ^ bit(y, 4)) << 7 |
            (bit(x, 6) ^ bit(y, 5)) << 8 | (bit(x, 5) ^ bit(y, 6)) << 9 | bit(y, 3) << 10 |
            bit(x, 4) << 11 | bit(y, 6) << 12 | bit(x, 6) << 13 | bit(x, 7) << 14 | bit(x, 8) << 15)


def detile(data):
    import numpy as np

    width, height = FRAME
    x = np.arange(width, dtype=np.int64)
    y = np.arange(height, dtype=np.int64)
    columns = (x // 512) * 65536 + tile_offset(x % 512, 0)
    rows = (y // 128) * 128 * width
    index = rows[:, None] + (columns[None, :] ^ tile_offset(0, y % 128)[:, None])
    pixels = np.frombuffer(data, dtype=np.uint32)[index]
    return pixels.astype("<u4").tobytes()


def pull_captures(seen):
    from PIL import Image

    try:
        sock = socket.create_connection((ps5.PS5_IP, CAPTURE_PORT), timeout=2)
    except OSError:
        return
    sock.settimeout(30)
    try:
        while True:
            (name_length,) = struct.unpack("<I", receive_exactly(sock, 4))
            if not name_length:
                break
            name = receive_exactly(sock, name_length).decode()
            (size,) = struct.unpack("<Q", receive_exactly(sock, 8))
            data = receive_exactly(sock, size)
            Image.frombuffer("RGBA", FRAME, detile(data), "raw", "RGBA", 0, 1).convert("RGB").save(OUT / f"{name}.png")
            seen.add(name)
            print(f"captured {name}", flush=True)
    except (OSError, ConnectionError) as e:
        print(f"capture pull interrupted: {e}", flush=True)
    finally:
        sock.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("plan")
    ap.add_argument("--timeout", type=int, default=120)
    args = ap.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*"):
        old.unlink()
    engine()
    install_launcher()
    plan = Path(args.plan).read_text().rstrip("\n") + f"\nserve {CAPTURE_PORT}\n"
    write_remote(TEST_PLAN, plan.encode())

    start = KLOG.stat().st_size
    print("launch:", get(f"{PLDMGR}/loadpayload:{LAUNCHER.name}", timeout=30).strip()[:200], flush=True)
    seen = set()
    deadline = time.time() + args.timeout
    exited = False
    try:
        while time.time() < deadline and not exited:
            pull_captures(seen)
            text = KLOG.read_bytes()[start:].decode(errors="replace")
            exited = "ps5: exit" in text or "Terminating pid" in text
            time.sleep(1)
    finally:
        push.http("POST", "/api/ps5/fs/delete", {"addr": push.XFER, "path": TEST_PLAN})

    text = KLOG.read_bytes()[start:].decode(errors="replace")
    lines = [l for l in text.splitlines() if l.strip() and not any(n in l for n in NOISE)]
    (OUT / "log.txt").write_text("\n".join(lines), encoding="utf-8")
    print("\n".join(lines[-80:]))
    print(f"\nexited={exited} captures={sorted(seen)}")


if __name__ == "__main__":
    main()
