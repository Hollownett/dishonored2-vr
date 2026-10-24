"""v609 G6: corpses use the corpse strength, capped props keep the v608 scale. Pins the lane routing in src/native_grab.hpp."""
from pathlib import Path


def check(source: str) -> None:
    assert "GrabTuning prop_tuning_" in source, "NativeGrab keeps the prop tuning as its own copy"
    assert "in.tuning=corpse?&tuning_:&prop_tuning_;" in source, "the corpse motor takes the corpse tuning, props the prop tuning"
    configure = source[source.index("void configure_corpse("):source.index("CorpseDiagnostics& corpse_diagnostics()")]
    assert "prop_tuning_=tuning_.prop_view();" in configure, "configure_corpse refreshes the prop tuning from the corpse tuning"
    assert "tuning_=options.tuning;" in configure


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    source = (root / "src" / "native_grab.hpp").read_text(encoding="utf-8-sig")
    check(source)
    mutations = (
        source.replace("in.tuning=corpse?&tuning_:&prop_tuning_;", "in.tuning=&tuning_;", 1),
        source.replace("prop_tuning_=tuning_.prop_view();", "prop_tuning_=tuning_;", 1),
    )
    for index, mutation in enumerate(mutations):
        assert mutation != source, f"Mutation {index} did not reach its production target"
        try:
            check(mutation)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f"Unsafe production mutation {index} was accepted")
    print("PASS corpse/prop tuning routing and negative source mutations")
