from pathlib import Path

# v1027: the native-eye-capture readback (capture_xr_transfer) is a DIAGNOSTIC. It used to throw out of the render callback, and the render callback's
# catch runs recover_xr_session (dxgi_probe.cpp): the v1024 session of 2026-11-05 logged "Native and processed eye dimensions differ" from it and lost its
# XR session 4 s later, mid-startup, with no exception record and no Windows crash report. A diagnostic must never cost a session.
# v1028: a diagnostic must not cost frames either. The capture throws AFTER three full-frame readbacks and two 31 MB writes when the processed eye image
# is not the native one or differs in size (the default uncant/SSAA chain); the throw skipped the mask update, so the same impossible capture ran on
# every frame (v1027 session: eye_transfer_observer 37.95 ms p50 / 46.70 ms p95, 20 FPS). A refusal or fault now latches for the pose epoch and an
# impossible size is refused from the texture descriptions before any readback.
def check(probe: str) -> None:
    wrapper = probe.index("void capture_xr_transfer(UINT eye,ID3D11Texture2D* source,ID3D11Texture2D* destination")
    body = probe[wrapper:wrapper + 900]
    assert ") noexcept {" in body, "the diagnostic capture must be noexcept"
    assert "capture_xr_transfer_unsafe(eye,source,destination,layer,gameplay,tracked,native_source);" in body
    assert "catch(const std::exception& error) {latch_transfer_capture(gameplay,error.what());}" in body
    assert 'catch(...) {latch_transfer_capture(gameplay,"unknown diagnostic fault");}' in body
    # the latch is one-way inside an epoch: a second refusal costs nothing, and the first one is what is counted and noted
    latch = probe.index("static void latch_transfer_capture(bool gameplay,const char* why) noexcept {")
    latch_body = probe[latch:latch + 700]
    assert "if(refused) return;" in latch_body
    assert "xr_transfer_capture_refused.fetch_add(1,std::memory_order_relaxed)" in latch_body
    assert "xr_transfer_capture_skipped.fetch_add(1,std::memory_order_relaxed)==0" in latch_body  # one note, the rest counted
    unsafe = probe.index("void capture_xr_transfer_unsafe(UINT eye")
    unsafe_end = probe.index("void capture_xr_transfer(UINT eye")
    capture = probe[unsafe:unsafe_end]
    # an impossible pair is refused from the descriptions, before the readbacks and the 31 MB writes
    readbacks = capture.index("const auto game=diag_readback_rgba(source),xr=diag_readback_rgba(destination);")
    assert capture.index("native_source->GetDesc(&native_desc)") < readbacks
    assert 'latch_transfer_capture(gameplay,"the processed eye image and the native eye image differ in size")' in capture
    assert 'latch_transfer_capture(gameplay,"the eye image and the XR eye copy differ in size")' in capture
    # the latch is consulted before the capture work, cleared per pose epoch, and re-armed only by an explicit request
    assert "if(xr_transfer_refused[capture_slot]) return;" in capture
    assert "xr_transfer_refused[0]=xr_transfer_refused[1]=false;" in capture
    assert "xr_transfer_refused[capture_slot]=false; // v1028: an explicit request re-arms the diagnostic" in capture
    # the unsafe body keeps the two diagnostic throws it had (they are what the wrapper now latches)
    assert 'throw std::runtime_error("Native and processed eye dimensions differ")' in capture
    assert 'throw std::runtime_error("XR base transfer pixels differ before overlay composition")' in capture
    # the render callback still routes it as the transfer observer, and the recovery path is still the only caller of recover_xr_session
    assert "game_xr->observe_transfer=&capture_xr_transfer;" in probe
    assert "recover_xr_session(error.what())" in probe
    # the skips and the refusals are visible in the live status
    assert ',\\"image_transfer_capture_skipped\\":' in probe
    assert ',\\"image_transfer_capture_refused\\":' in probe


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src/dxgi_probe.cpp").read_text(encoding="utf-8"))
    print("eye_capture_routing_check ok")
