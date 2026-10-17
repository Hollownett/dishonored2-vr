import re
from pathlib import Path


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    open_brace = source.index("{", start)
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace:index + 1]
    raise ValueError("unterminated function body")


def check(source: str) -> None:
    for text in ("Cannot save live XR session state", "Cannot save XR lifecycle state"):
        assert text not in source, f"diagnostic write failure must not throw: {text}"
    assert "static std::atomic<bool> xr_status_write_failed{};" in source
    assert "xr_status_write_failed=true;" in source
    assert '\\"xr_status_write_failed\\":' in source, "status JSON must report xr_status_write_failed"
    assert "frame_timing_queue.reset();frame_timing_write_failed=false;xr_status_write_failed=false;" in source, \
        "xr_status_write_failed must reset where frame_timing_write_failed resets"
    removal = function_body(source, "void remove_glyph_hook()")
    assert "MH_RemoveHook" not in removal, "remove_glyph_hook must not free the trampoline"
    assert not re.search(r"original_create_texture2d\s*=\s*nullptr", removal), \
        "remove_glyph_hook must keep the trampoline pointer streaming threads call through"
    assert "MH_DisableHook(create_texture2d_target)" in removal
    install = function_body(source, "void install_glyph_hook(")
    assert "MH_EnableHook(create_texture2d_target)" in install, "install must re-enable the retained hook"


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    source = (root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8-sig")
    check(source)
    mutations = (
        source.replace("xr_status_write_failed=true;", 'throw std::runtime_error("Cannot save live XR session state");', 1),
        source.replace("MH_DisableHook(create_texture2d_target);", "MH_DisableHook(create_texture2d_target);MH_RemoveHook(create_texture2d_target);", 1),
        source.replace("MH_DisableHook(create_texture2d_target);", "MH_DisableHook(create_texture2d_target);original_create_texture2d=nullptr;", 1),
        source.replace("MH_EnableHook(create_texture2d_target)", "MH_EnableHook(nullptr)", 1),
        source.replace("frame_timing_write_failed=false;xr_status_write_failed=false;", "frame_timing_write_failed=false;", 1),
    )
    for index, mutation in enumerate(mutations):
        assert mutation != source, f"Mutation {index} did not reach its production target"
        try:
            check(mutation)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f"Unsafe production mutation {index} was accepted")
    print("PASS diagnostics cannot throw into XR recovery and the glyph hook is only disabled, with negative source mutations")
