from pathlib import Path


root = Path(__file__).resolve().parents[1]
upload = (root / "src/dxgi_probe.cpp").read_text(encoding="utf-8")
observer = (root / "src/synchronized_observer.hpp").read_text(encoding="utf-8")
attachment = (root / "src/native_attachment_pose.hpp").read_text(encoding="utf-8")

terminal = upload[upload.index("if(submitted_socket_token &&"):]
assert terminal.index("submitted=source;") < terminal.index("socket_tracked_submitted=full")
assert terminal.index("socket_tracked_submitted=full") < terminal.index("original_arm_upload(model,submitted,count);")
assert "diagnostic->socket_stage=3" in terminal
assert "diagnostic->hand_proxies_valid=false" in terminal
assert '\\"socket_terminal\\"' in observer
assert "kind>=last_.size()" in observer
assert "synchronized_observer.begin_palette(observed_kind,GetTickCount64())" in upload
assert "synchronized_observer.begin(observed_kind" not in upload
assert "return kind<3 && (!compact_only_" in observer
assert "kind==1)?begin(kind,tick):std::nullopt;" in observer
assert "std::array<SynchronizedConsumer,128> ring_" in observer
assert "tick-last_[kind]<100" in observer
cleanup = attachment[attachment.index("const auto cleanup=[]"):]
assert cleanup.index("value->unlock();") < cleanup.index("value->observed_source.observe_socket(")
assert cleanup.index("admission.release(") < cleanup.index("value->observed_source.observe_socket(")
assert "if(joint!=59)return;" in upload
assert "3u+receipt.diagnostic_side" in upload
assert "receipt.diagnostic_socket_token" in upload
flush = upload[upload.index("static void flush_synchronized("):upload.index("static d2vr::rig::BodyMaterialOverride")]
assert "write_synchronized_bounded(" in flush
assert "synchronized_bytes+bytes.size()>64ull*1024*1024" not in flush
assert "synchronized_compact_exhausted=true" in flush
assert "synchronized_observer.continue_compact()" in flush
assert 'synchronized_log.open(output/L"synchronized-diagnostics.jsonl",std::ios::binary|std::ios::trunc)' in upload
assert "synchronized_log.flush();if(!synchronized_log)synchronized_write_failed=true;" in flush
assert "synchronized_log.close();if(!synchronized_log)synchronized_write_failed=true;" in flush
print("PASS static terminal diagnostic ordering and bounded observer wiring")
