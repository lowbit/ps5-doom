"""Uploads the built title folder to /data/homebrew on the PS5, or prints a file from the console.

    uv run --no-project python tools/deploy.py            upload dist/PPSA99666
    uv run --no-project python tools/deploy.py cat PATH   print a console file (e.g. the game log)

Uses the standalone PS5Upload engine from dev/ps5/tools/push_ffpkg.py, so the PS5Upload desktop
app can stay closed. The PS5Upload helper payload must be running on the console.
"""
import hashlib
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / "tools"))
import ps5  # noqa: E402
import push_ffpkg as push  # noqa: E402

TITLE = "PPSA99666"
APP = ROOT / "dist" / TITLE
DEST = f"/data/homebrew/{TITLE}"


def upload():
    if not (APP / "eboot.bin").exists():
        sys.exit(f"{APP} is missing: run make ps5 first")
    push.LOG = ROOT / "build" / "deploy.log"
    push.LOG.parent.mkdir(exist_ok=True)
    push.start_engine()
    push.ensure_helper()

    digest = hashlib.md5()
    for f in sorted(APP.rglob("*")):
        if f.is_file():
            digest.update(f"{f.relative_to(APP)}|{f.stat().st_size}|{f.stat().st_mtime_ns}".encode())
    r = push.http("POST", "/api/transfer/dir",
                  {"addr": push.XFER, "tx_id": digest.hexdigest(), "dest_root": DEST, "src_dir": str(APP)})
    job = r.get("job_id")
    if not job:
        sys.exit(f"transfer did not start: {r}")
    while True:
        j = push.http("GET", f"/api/jobs/{job}")
        status = j.get("status") or j.get("job", {}).get("status")
        if status in ("done", "failed"):
            break
        time.sleep(2)
    if status != "done":
        sys.exit(f"upload failed: {j}")
    names = sorted(e["name"] for e in ps5.list_dir(DEST)["entries"])
    print(f"uploaded to {DEST}: {', '.join(names)}")


def cat(path):
    sys.stdout.buffer.write(ps5.read(path))


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "cat":
        cat(sys.argv[2])
    elif len(sys.argv) == 1:
        upload()
    else:
        sys.exit(__doc__)
