#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/controller_input.hpp"
#include "../src/turn_control.hpp"
#include "../src/head_locomotion.hpp"
#include <cassert>
#include <iostream>
#include <limits>

static UINT connection_events{},removal_events{};
LRESULT CALLBACK input_window(HWND window,UINT message,WPARAM code,LPARAM data) {
    if(message==WM_DEVICECHANGE) {
        auto notice=reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W*>(data);
        assert(notice && notice->dbcc_size==sizeof(*notice) && notice->dbcc_devicetype==DBT_DEVTYP_DEVICEINTERFACE);
        XINPUT_STATE state{};
        if(code==DBT_DEVICEARRIVAL) {++connection_events;assert(XInputGetState(0,&state)==ERROR_SUCCESS);}
        if(code==DBT_DEVICEREMOVECOMPLETE) ++removal_events;
        return TRUE;
    }
    return DefWindowProcW(window,message,code,data);
}

std::uintptr_t own_get_state_import(std::uintptr_t base) {
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    auto entry=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress);
    for(;entry->Name;++entry) {
        auto name=reinterpret_cast<const char*>(base+entry->Name);
        if(_stricmp(name,"XINPUT9_1_0.dll")) continue;
        auto lookup=reinterpret_cast<const IMAGE_THUNK_DATA64*>(base+entry->OriginalFirstThunk);
        for(std::uintptr_t index=0;lookup[index].u1.AddressOfData;++index) {
            if(IMAGE_SNAP_BY_ORDINAL64(lookup[index].u1.Ordinal)) continue;
            auto imported=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base+lookup[index].u1.AddressOfData);
            if(!std::strcmp(reinterpret_cast<const char*>(imported->Name),"XInputGetState"))
                return entry->FirstThunk+index*sizeof(void*);
        }
    }
    throw std::runtime_error("Test executable has no native GetState import");
}

int main() {
    d2vr::PadComposer composer;
    d2vr::ControllerInput input{};input.active=true;
    auto pad=composer.compose(input,true);
    assert(pad.wButtons==0 && pad.sThumbLY==0);
    input.move_y=1;input.turn_y=.8f;input.right_trigger=.7f;input.a=true;
    pad=composer.compose(input,true);
    assert(pad.sThumbLY==32767 && pad.sThumbRY==0 && pad.bRightTrigger>170 && pad.wButtons==XINPUT_GAMEPAD_A);
    pad=composer.compose(input,false);
    assert(pad.bRightTrigger==0 && pad.sThumbRY>25000);
    pad=composer.compose(input,true);
    assert(pad.wButtons==0 && pad.bRightTrigger==0 && pad.sThumbLY==0);
    input={};input.active=true;composer.compose(input,true);
    input.right_grip=1;input.left_grip=1;input.turn_y=-1;
    pad=composer.compose(input,true);
    assert((pad.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER) && (pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER) && pad.sThumbRY==-32767);
    input.move_x=std::numeric_limits<float>::quiet_NaN();input.right_trigger=std::numeric_limits<float>::infinity();
    pad=composer.compose(input,true);assert(pad.sThumbLX==0 && pad.bRightTrigger==0);
    input.active=false;pad=composer.compose(input,true);
    const XINPUT_GAMEPAD neutral{};assert(!std::memcmp(&pad,&neutral,sizeof(pad)));

    // A short Quest menu tap has a native pollable pulse after release.
    d2vr::PadComposer menu;input={};input.active=true;menu.compose(input,true,1000);
    input.menu=true;assert(!(menu.compose(input,true,1010).wButtons&XINPUT_GAMEPAD_START));
    input.menu=false;assert(menu.compose(input,true,1020).wButtons&XINPUT_GAMEPAD_START);
    assert(menu.compose(input,false,1169).wButtons&XINPUT_GAMEPAD_START);
    assert(!(menu.compose(input,false,1170).wButtons&XINPUT_GAMEPAD_START));
    input.menu=true;menu.compose(input,false,1200);input.active=false;menu.compose(input,false,1210);
    input.active=true;assert(!menu.compose(input,false,1220).wButtons);
    input.menu=false;assert(!menu.compose(input,false,1230).wButtons);
    input.menu=true;menu.compose(input,false,1300);input.menu=false;
    assert(menu.compose(input,false,1310).wButtons&XINPUT_GAMEPAD_START);
    assert(!(menu.compose(input,true,1320).wButtons&XINPUT_GAMEPAD_START)); // Resume cannot re-pause.
    assert(!menu.compose(input,true,1200).wButtons); // Clock reset cancels the pulse.

    // Chord cancellation affects Pause only: native Back and all four
    // shortcuts remain pollable, with no duplicate Start on release.
    d2vr::PadComposer chords;input={};input.active=true;chords.compose(input,true,2000);
    input.menu=true;chords.compose(input,true,2010);
    input.journal=input.suppress_pause=input.menu_modifier=true;
    input.move_x=.8f;input.turn_x=.9f;input.left_trigger=.8f;input.right_grip=.9f;
    pad=chords.compose(input,true,2020);
    assert(pad.wButtons==XINPUT_GAMEPAD_BACK && !pad.sThumbLX && !pad.sThumbRX && !pad.bLeftTrigger);
    input.menu=input.suppress_pause=input.menu_modifier=false;
    pad=chords.compose(input,true,2030);
    assert(pad.wButtons==XINPUT_GAMEPAD_BACK && !pad.sThumbLX && !pad.sThumbRX && !pad.bLeftTrigger);
    input.journal=false;input.move_x=input.turn_x=input.left_trigger=input.right_grip=0;chords.compose(input,true,2040);
    input.move_x=.8f;pad=chords.compose(input,true,2050);assert(pad.sThumbLX!=0);
    input.move_x=0;chords.compose(input,true,2060);
    constexpr WORD directions[]{XINPUT_GAMEPAD_DPAD_RIGHT,XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_DOWN};
    for(unsigned i=0;i<4;++i) {
        input.dpad[i]=true;pad=chords.compose(input,true,2070+i*20);assert(pad.wButtons==directions[i]);
        input.dpad[i]=false;assert(!chords.compose(input,true,2080+i*20).wButtons);
    }
    input.menu=true;chords.compose(input,false,2160);input.menu=false;
    assert(chords.compose(input,false,2170).wButtons&XINPUT_GAMEPAD_START);
    input.turn_x=.7f;input.turn_y=-.7f;
    pad=chords.compose(input,false,2180);assert(pad.sThumbRX!=0 && pad.sThumbRY!=0); // UI stick outside modifier.

    // Y-held Quest right stick maps to native LEFT lean axes, including up. Releasing
    // Y with a deflected stick cannot become camera input until real center.
    d2vr::PadComposer lean;input={};input.active=true;lean.compose(input,true,3000);
    input.y=true;input.turn_x=.8f;input.turn_y=.9f;input.move_x=-1;input.move_y=-1;
    assert(d2vr::native_directional_stick(input));
    pad=lean.compose(input,true,3010);
    assert((pad.wButtons&XINPUT_GAMEPAD_Y) && pad.sThumbLX>25000 && pad.sThumbLY>29000 && !pad.sThumbRX && !pad.sThumbRY);
    input.turn_x=-.8f;input.turn_y=-.9f;pad=lean.compose(input,true,3020);
    assert(pad.sThumbLX<-25000 && pad.sThumbLY<-29000 && !pad.sThumbRX && !pad.sThumbRY);
    input.y=false;assert(!d2vr::native_directional_stick(input));
    pad=lean.compose(input,true,3030);assert(!pad.sThumbLX && !pad.sThumbLY && !pad.sThumbRX && !pad.sThumbRY);
    input.turn_x=input.turn_y=input.move_x=input.move_y=0;lean.compose(input,true,3040);
    input.turn_x=.8f;input.turn_y=.9f;pad=lean.compose(input,true,3050);
    assert(pad.sThumbRX>25000 && pad.sThumbRY==0);
    input.menu_modifier=true;input.y=true;assert(!d2vr::native_directional_stick(input));
    pad=lean.compose(input,true,3060);assert(!pad.sThumbRX && !pad.sThumbRY);
    input.active=false;lean.compose(input,true,3070);
    input.active=true;input.menu_modifier=false;pad=lean.compose(input,true,3080);
    assert(!(pad.wButtons&XINPUT_GAMEPAD_Y) && !pad.sThumbRX && !pad.sThumbRY);
    for(auto mode:{d2vr::TurnControl::Mode::snap,d2vr::TurnControl::Mode::smooth}) {
        d2vr::TurnControl turn;turn.configure({mode,45,90});
        turn.publish(0,true,true,true,true,3100,1);
        turn.publish(1,true,true,true,true,3101,1);
        input={};input.active=input.y=true;input.turn_x=1;
        assert(!turn.publish(input.turn_x,true,true,!d2vr::native_directional_stick(input),true,3102,1));
        assert(!turn.consume(.01,true,3103,1,1)); // Pending turn cancelled by lean.
        input.y=false;turn.publish(input.turn_x,true,true,!d2vr::native_directional_stick(input),true,3104,1);
        assert(!turn.consume(.01,true,3105,1,2)); // No held exit turn.
        turn.publish(0,true,true,true,true,3106,1);turn.publish(1,true,true,true,true,3107,1);
        assert(turn.consume(.01,true,3108,1,3)); // Actual center/flick re-arms.
    }
    d2vr::HeadFollowing following;following.publish(.3,3200,1,2,3,4,true);following.commit(.1);
    assert(following.delta(.01,false,3201)==0 && std::abs(following.compensation(1)-.1)<1e-9);
    assert(following.delta(.01,true,3202)>0); // Lean suspends body-follow, retains compensation.
    for(bool wheel:{false,true}) {
        d2vr::PadComposer stale;input={};input.y=!wheel;input.left_grip=wheel?1.f:0.f;
        stale.compose(input,true,3300);input.active=true;stale.compose(input,true,3310);
        input.turn_x=input.turn_y=.8f;pad=stale.compose(input,true,3320);
        assert(!(pad.wButtons&(XINPUT_GAMEPAD_Y|XINPUT_GAMEPAD_LEFT_SHOULDER)));
        assert(!pad.sThumbLX && !pad.sThumbLY && !pad.sThumbRX && !pad.sThumbRY); // Held focus-blocked modifier cannot move/LOOK.
        if(!wheel) {input.move_x=input.move_y=1;pad=stale.compose(input,true,3321);assert(!pad.sThumbLX && !pad.sThumbLY && !pad.sThumbRX && !pad.sThumbRY);}
        input.y=false;input.left_grip=0;pad=stale.compose(input,true,3330);
        assert(!pad.sThumbRX && !pad.sThumbRY); // Deflected exit must still observe center.
        if(!wheel) assert(!pad.sThumbLX && !pad.sThumbLY);
        input.turn_x=input.turn_y=input.move_x=input.move_y=0;stale.compose(input,true,3340);
        input.turn_x=.8f;pad=stale.compose(input,true,3350);assert(pad.sThumbRX>25000);
        input.turn_x=0;stale.compose(input,true,3360);
        input.y=!wheel;input.left_grip=wheel?1.f:0.f;input.turn_x=input.turn_y=.8f;
        pad=stale.compose(input,true,3370);
        if(wheel) assert(pad.sThumbRX>25000 && pad.sThumbRY>25000);
        else assert(pad.sThumbLX>25000 && pad.sThumbLY>25000 && !pad.sThumbRX && !pad.sThumbRY);
    }

    // Real native IAT lifecycle on this test executable, using the Windows
    // XInput DLL and its original return path. No game process is involved.
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto rva=own_get_state_import(base);
    const auto original=*reinterpret_cast<void**>(base+rva);
    XINPUT_STATE state{};
    const auto other_index_result=XInputGetState(3,&state);
    d2vr::XInputBridge bridge;bridge.install(base,rva);
    WNDCLASSW window_class{};window_class.lpfnWndProc=&input_window;window_class.hInstance=GetModuleHandleW(nullptr);
    window_class.lpszClassName=L"D2VR controller connection check";assert(RegisterClassW(&window_class));
    HWND window=CreateWindowExW(0,window_class.lpszClassName,L"",WS_POPUP,0,0,64,64,nullptr,nullptr,window_class.hInstance,nullptr);
    assert(window);bridge.set_game_window(window);
    input={};input.active=true;bridge.publish(input,true);
    assert(connection_events==1 && bridge.connection_attempted() && bridge.connection_notified());
    input.move_y=1;input.a=true;input.right_trigger=.7f;bridge.publish(input,true);
    assert(XInputGetState(0,&state)==ERROR_SUCCESS && state.Gamepad.sThumbLY==32767 && state.Gamepad.bRightTrigger>170);
    const auto packet=state.dwPacketNumber;
    assert(XInputGetState(0,&state)==ERROR_SUCCESS && state.dwPacketNumber==packet);
    assert(XInputGetState(3,&state)==other_index_result);
    input={};input.active=true;bridge.publish(input,true);
    input.y=true;input.move_x=input.move_y=1;input.turn_x=.8f;input.turn_y=-.9f;bridge.publish(input,true);
    assert(XInputGetState(0,&state)==ERROR_SUCCESS && (state.Gamepad.wButtons&XINPUT_GAMEPAD_Y));
    assert(state.Gamepad.sThumbLX>25000 && state.Gamepad.sThumbLY<-29000 && !state.Gamepad.sThumbRX && !state.Gamepad.sThumbRY);
    Sleep(270);
    assert(XInputGetState(0,&state)==ERROR_SUCCESS && !std::memcmp(&state.Gamepad,&neutral,sizeof(neutral)) && state.dwPacketNumber!=packet);
    bridge.publish({},false);
    assert(XInputGetState(0,&state)==ERROR_SUCCESS && !std::memcmp(&state.Gamepad,&neutral,sizeof(neutral)));
    assert(bridge.virtual_reads()>=4 && bridge.reads()>=5);
    assert(bridge.disable() && *reinterpret_cast<void**>(base+rva)==original);
    assert(connection_events==1 && removal_events==1);
    assert(XInputGetState(3,&state)==other_index_result);
    bridge.install(base,rva);
    XInputGetState(0,&state);
    assert(bridge.reads()==1 && bridge.virtual_reads()==0 && bridge.nonzero_reads()==0);
    assert(bridge.disable() && *reinterpret_cast<void**>(base+rva)==original);
    DestroyWindow(window);UnregisterClassW(window_class.lpszClassName,window_class.hInstance);
    {   // A grip squeezed in a menu is one shoulder tap (menus repeat a held shoulder after 225 ms); one begun in gameplay holds.
        d2vr::PadComposer taps;d2vr::ControllerInput g{};g.active=true;taps.compose(g,false,1000);
        const auto rb=[&](bool play,std::uint64_t t) {return (taps.compose(g,play,t).wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;};
        const auto lb=[&](bool play,std::uint64_t t) {return (taps.compose(g,play,t).wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;};
        g.right_grip=1;
        assert(rb(false,1000) && rb(false,1000+d2vr::PadComposer::menu_shoulder_tap_ms-1) && !rb(false,1225) && !rb(false,2000));
        g.right_grip=0;taps.compose(g,false,2100);g.right_grip=1;assert(rb(false,2200));   // the next squeeze taps again
        g.right_grip=0;taps.compose(g,true,2900);g.left_grip=1;
        assert(lb(true,3000) && lb(false,4000));   // held from gameplay into the power wheel (not gameplay): still LB
    }
    std::cout << "PASS: VR pad mapping, native window connection probe through patched XInput, menu/focus/stale release and import restoration, menu grip taps\n";
}
