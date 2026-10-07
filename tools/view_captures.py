"""Convert bounded native D3D11 RGBA diagnostic frames to PNG (Pillow required)."""
import argparse
import json
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--result", type=Path, default=Path("artifacts/dxgi-probe-result.json"))
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[1]
    result_path = args.result.resolve()
    if not result_path.is_relative_to(workspace):
        parser.error("Only workspace diagnostic artifacts are accepted")
    report = json.loads(result_path.read_text(encoding="utf-8-sig"))
    width, height = report.get("width"), report.get("height")
    if (type(width) is not int or type(height) is not int or
            not 1 <= width <= 4096 or not 1 <= height <= 4096 or report.get("format") not in (28,29)):
        parser.error("Expected bounded RGBA8 D3D11 captures")
    for label in ("mono", "left", "stereo"):
        if not report.get(label+"_captured"):
            print(label+": no frame captured")
            continue
        source = result_path.parent / ("dxgi-"+label+".rgba")
        if source.stat().st_size != width*height*4:
            parser.error("RGBA capture size differs from its swapchain report")
        image = Image.frombytes("RGBA",(width,height),source.read_bytes()).convert("RGB")
        destination = source.with_suffix(".png")
        image.save(destination)
        print(destination)


if __name__ == "__main__":
    main()
