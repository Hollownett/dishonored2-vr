#include "../src/simulated_load_confirm.hpp"
#include "../src/controller_input.hpp"
#include <cassert>
#include <iostream>
int main() {
    d2vr::SimulatedLoadConfirm confirm;
    assert(!confirm.sample(0,true,true));assert(!confirm.sample(499,true,true));
    assert(confirm.sample(500,true,true));assert(confirm.sample(699,true,true));
    assert(!confirm.sample(700,true,true));assert(!confirm.sample(120000,true,true));
    assert(!confirm.sample(120001,false,true));
    assert(!confirm.sample(130000,true,true));assert(!confirm.sample(130499,true,true));
    assert(confirm.sample(130500,true,true));assert(!confirm.sample(130700,true,true));
    // Losing focus aborts a pulse; returning needs neutral priming again.
    confirm.reset();assert(!confirm.sample(200000,true,true));assert(confirm.sample(200500,true,true));
    assert(!confirm.sample(200550,true,false));assert(!confirm.sample(200560,true,true));
    assert(!confirm.sample(201059,true,true));assert(confirm.sample(201060,true,true));
    assert(!confirm.sample(201260,true,true));
    // Backward clocks also reset; sparse sampling still emits one full window.
    assert(!confirm.sample(10,true,true));assert(!confirm.sample(509,true,true));
    assert(confirm.sample(2000,true,true));assert(confirm.sample(2199,true,true));
    assert(!confirm.sample(2200,true,true));assert(!confirm.sample(8000,true,true));
    confirm.reset();assert(!confirm.sample(9000,true,false));assert(!confirm.sample(9001,false,true));
    assert(!confirm.sample(9002,true,true));assert(confirm.sample(9502,true,true));
    assert(!confirm.sample(9503,false,true));assert(!confirm.sample(9504,true,true));
    // Exercise the actual activation-release gate, not just the fixture bool.
    d2vr::PadComposer composer;d2vr::ControllerInput input{};input.active=true;confirm.reset();
    for(unsigned tick=10000;tick<=10700;tick+=25) {
        input.a=confirm.sample(tick,true,true);const auto pad=composer.compose(input,false,tick);
        assert(bool(pad.wButtons&XINPUT_GAMEPAD_A)==(tick>=10500 && tick<10700));
    }
    input.a=confirm.sample(10725,false,true);assert(!(composer.compose(input,true,10725).wButtons&XINPUT_GAMEPAD_A));
    // A later checkpoint gets its own neutral activation and fresh confirm.
    input.active=false;composer.compose(input,false,11000);confirm.sample(11000,true,false);
    input.active=true;
    for(unsigned tick=12000;tick<=12700;tick+=25) {
        input.a=confirm.sample(tick,true,true);const auto pad=composer.compose(input,false,tick);
        assert(bool(pad.wButtons&XINPUT_GAMEPAD_A)==(tick>=12500 && tick<12700));
    }
    std::cout<<"PASS: initial/late/repeated ready confirm, neutral500ms priming, one200ms pulse, focus/leave/clock reset and sparse frames\n";
}
