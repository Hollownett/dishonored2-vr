from pathlib import Path


def body(source: str) -> str:
    start = source.index("void invoke(")
    end = source.index("static void __fastcall step(", start)
    return source[start:end]


def check(source: str) -> None:
    invoke = body(source)
    call = invoke.index("original_(delegate,")
    unlock = invoke.find("feedback_lock.unlock()")
    assert 0 <= unlock < call, "feedback lock must be released before the native physics step"
    relock = invoke.find("feedback_lock.lock()", call)
    assert relock > call, "feedback lock must be re-acquired after the native physics step"
    assert "const bool relock=feedback_lock.owns_lock();" in invoke[:call]
    assert "if(relock) feedback_lock.unlock();" in invoke[:call]
    tail = invoke[call:]
    assert "if(relock) {feedback_lock.lock();" in tail
    assert tail.index("feedback_.pending()") > tail.index("feedback_lock.lock()"), "pending() must be read under the lock"


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    source = (root / "src" / "native_turning.hpp").read_text(encoding="utf-8-sig")
    check(source)
    mutations = (
        source.replace("feedback_lock.unlock();", "", 1),
        source.replace("feedback_lock.lock();", "", 1),
        source.replace("if(relock) {feedback_lock.lock();", "if(relock) {", 1),
    )
    for index, mutation in enumerate(mutations):
        assert mutation != source, f"Mutation {index} did not reach its production target"
        try:
            check(mutation)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f"Unsafe production mutation {index} was accepted")
    print("PASS native turning releases feedback lock across the native step, with negative source mutations")
