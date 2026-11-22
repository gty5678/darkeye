"""Encode the baseline recorder's deterministic PNG sequence as animated GIF.

Usage: python tools/qrhi3d_baseline_animation.py build/qrhi3d-baseline
Requires Pillow. The JSON and original PNG frames remain authoritative.
"""
import argparse
import json
from pathlib import Path
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    manifest = json.loads((root / "baseline.json").read_text(encoding="utf-8"))
    frames = []
    for relative in manifest["frames"]:
        with Image.open(root / relative) as image:
            frames.append(image.resize((640, 360)).convert("RGB"))
    if not frames:
        raise RuntimeError("The baseline manifest has no animation frames")
    frames[0].save(root / "interaction.gif", save_all=True,
                   append_images=frames[1:], duration=manifest["frameDurationMs"], loop=0)


if __name__ == "__main__":
    main()
