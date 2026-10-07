"""Find geometric camera-layout candidates in an already bounded object read."""
import math
import struct


def camera_field_candidates(data: bytes, limit: int = 64) -> list[int]:
    if limit < 1:
        raise ValueError("Candidate limit must be positive")
    if len(data) % 4:
        raise ValueError("Object data must consist of complete 32-bit fields")
    values = struct.unpack(f"<{len(data)//4}f", data)
    matches = []
    for index in range(len(values)-19):
        fov = values[index]
        if not 0.05 < fov < 3.13:
            continue
        position = values[index+2:index+5]
        if not all(math.isfinite(v) and abs(v) < 1e7 for v in position) or max(map(abs, position)) < 1e-4:
            continue
        basis = values[index+11:index+20]
        if not all(math.isfinite(v) and abs(v) <= 1.02 for v in basis):
            continue
        axes = [basis[i:i+3] for i in (0,3,6)]
        if not all(abs(sum(v*v for v in axis)-1) < 0.02 for axis in axes):
            continue
        if not all(abs(sum(a*b for a,b in zip(axes[i], axes[j]))) < 0.02 for i,j in ((0,1),(0,2),(1,2))):
            continue
        matches.append(index*4)
        if len(matches) >= limit:
            break
    return matches


def changed_words(before: bytes, after: bytes) -> list[int]:
    if len(before) != len(after) or len(before) % 4:
        raise ValueError("Snapshots must contain the same complete 32-bit fields")
    if before == after:
        return []
    count = len(before)//4
    old, new = struct.unpack(f"<{count}I", before), struct.unpack(f"<{count}I", after)
    return [index*4 for index, (a,b) in enumerate(zip(old,new)) if a != b]
