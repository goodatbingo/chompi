#!/usr/bin/env python3
"""Stage factory SD-card content for Tape Bench.

The page copies these files onto each engine's RAM-disk SD card. They come
straight from firmware/card-profiles; nothing is duplicated in git.

  python3 card.py link         web/card -> symlinks into card-profiles (for make serve)
  python3 card.py copy <dir>   copy only the files the page uses into <dir>/card (for Pages)

Both write <...>/card/manifest.json, which the page reads.
"""
import json
import os
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROFILES = HERE.parent / "firmware" / "card-profiles"

# What each engine's card gets. TAPE ships bank A of both voice modes; its
# other banks are on the real card profile if you want them locally.
SOURCES = {
    "tape": ("tape-2.0", lambda p: p.suffix == ".wav" and p.name[6] == "a"),
    "wave": ("wave-1.0", lambda p: p.suffix == ".wav"),
    "tempo": ("tempo-1.0", lambda p: p.suffix == ".wav" or p.name == "options.json"),
}


def files_for(engine):
    profile, keep = SOURCES[engine]
    root = PROFILES / profile
    out = []
    for p in sorted(root.rglob("*")):
        if p.is_file() and keep(p):
            out.append({"path": p.relative_to(root).as_posix(), "size": p.stat().st_size})
    return profile, out


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "link"
    dest = (Path(sys.argv[2]) if mode == "copy" else HERE / "web") / "card"
    dest.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for engine in SOURCES:
        profile, files = files_for(engine)
        manifest[engine] = files
        target = dest / engine
        if mode == "link":
            if target.is_symlink() or target.exists():
                target.unlink() if target.is_symlink() else shutil.rmtree(target)
            target.symlink_to(os.path.relpath(PROFILES / profile, dest))
        else:
            for f in files:
                out = target / f["path"]
                out.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(PROFILES / profile / f["path"], out)
    (dest / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    total = sum(f["size"] for fs in manifest.values() for f in fs)
    print(f"{mode}: {dest} ({total / 1e6:.0f} MB across {sum(map(len, manifest.values()))} files)")


if __name__ == "__main__":
    main()
