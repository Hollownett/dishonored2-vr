from pathlib import Path
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from camera_fields import camera_field_candidates, changed_words


def main():
    data = bytearray(512)
    fields = [1.0,0.0,-3175.0,124.0,72.0] + [0.0]*6 + [1.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0]
    struct.pack_into("<20f", data, 128, *fields)
    assert 128 in camera_field_candidates(data)
    for bad_offset, bad_value in ((128,0.0), (128,float("inf")), (136,float("nan")),
                                   (172,0.3), (176,1.0)):
        broken = bytearray(data)
        struct.pack_into("<f", broken, bad_offset, bad_value)
        assert 128 not in camera_field_candidates(broken)
    assert camera_field_candidates(bytes(76)) == []
    assert changed_words(data, data) == []
    changed = bytearray(data)
    struct.pack_into("<f", changed, 136, 80.0)
    struct.pack_into("<I", changed, 200, 0x7fc00001)
    assert changed_words(data, changed) == [136,200]
    try:
        camera_field_candidates(bytes(81))
    except ValueError:
        pass
    else:
        raise AssertionError("Misaligned object data accepted")
    print("PASS: camera-layout detection rejects invalid FoV, non-finite positions, and malformed bases")


if __name__ == "__main__":
    main()
