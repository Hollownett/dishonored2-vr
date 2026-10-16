"""Guard the passive left-weapon receipt lifecycle and native callback contract."""
from pathlib import Path
package = Path(__file__).resolve().parents[1]
held = (package / 'src/native_held_object.hpp').read_text()
owner = (package / 'src/native_left_weapon_socket_owner.hpp').read_text()
receipt = (package / 'src/left_weapon_render_receipt.hpp').read_text()
probe = (package / 'src/dxgi_probe.cpp').read_text()
inject = (package / 'tools/inject_probe.py').read_text()
assert 'inline static std::atomic<BindObserver> bind_observer{};' in held
hook = held[held.index('    static std::uintptr_t __fastcall bind_hook('):held.index('    static void place(')]
assert hook.count('r=bind_(rf,mode,pos,axis,lin,ang,dt);') == 1
position = 0
for token in ('++depth_;', 'const auto observe=bind_observer.load(', 'observe(0,', '__try {',
              'r=bind_(', 'observe(1,', 'place(rf,pos,axis,lin,ang,dt);', 'observe(2,',
              '__finally {', 'observe(3,', '--depth_;', 'return r;'):
    position = hook.index(token, position) + len(token)
disable = held[held.index('    static bool disable() noexcept {'):held.index('    // Render thread', held.index('    static bool disable() noexcept {'))]
assert disable.index('bind_observer=nullptr;') < disable.index('MH_DisableHook(') < disable.index('while(depth_')
diagnostic = owner[owner.index('    Diagnostic diagnostic() noexcept {'):owner.index('    void configure(')]
assert 'std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);' in diagnostic
assert 'current(' not in diagnostic and 'retire_locked(' not in diagnostic
assert 'std::array<Row,256> rows_' in receipt and 'if(id>32768)' in receipt
assert '64ull*1024*1024-32768' in receipt
assert 'source.model!=row.arm_model' in receipt and 'source.animation_identity!=row.animation_identity' in receipt
assert 'for(float value:local->f)if(!std::isfinite(value))row.local_valid=false;' in receipt
assert 'else out<<"null";' in receipt
assert 'placement_branch_selected' in receipt and 'observe(4,' in held
assert 'if(!reconfigurable_.exchange(false,std::memory_order_acq_rel))return false;' in receipt
assert 'if(!drained)drain_failed_=true;flush(drained);if(drained)reconfigurable_=true;' in receipt
assert 'log_.close();if(log_.fail())write_failed_=true;' in receipt
assert 'out.close();if(out.fail())write_failed_=true;' in receipt
assert 'bool failed()const noexcept' in receipt
assert '!d2vr::LeftWeaponRenderReceipt::process_owner().failed()' in probe
assert 'if(!substituted || !returned || !local)row.reasons|=64u;' in receipt
assert 'std::unique_lock<std::mutex> lock(queue_mutex_,std::try_to_lock);' in receipt
assert 'new ' not in receipt[receipt.index('    void bind('):receipt.index('    static void output_pose(')]
assert 'native_equipment_current' in receipt and 'entity+0xf0,row.model' in receipt
assert '+0x1860' not in receipt
assert probe.index('retire_bind_observer())throw') < probe.index('configure(&read_self,image_base,output)') < probe.index('bind_observer=&d2vr::LeftWeaponRenderReceipt::observe')
assert probe.count('&& left_weapon_receipt_drained && assassination_receipt_drained) started=false;') == 2
assert probe.count('const bool left_weapon_receipt_drained=left_weapon_receipt_finalizer.finish();') == 2
assert 'pad_disabled && controller_import_restored && left_weapon_receipt_drained &&' in probe
assert 'if(finished)return drained;finished=true;' in probe
assert 'synchronized_observer.enabled() || d2vr::LeftWeaponRenderReceipt::process_owner().enabled()' in probe
assert probe.index('.reader(receipt,animation,joint,stage,substituted,returned,local)') < probe.index('auto record=synchronized_observer.begin(3u+receipt.diagnostic_side')
assert 'left-weapon-bind-receipts.jsonl' in inject and 'left-weapon-bind-summary.json' in inject
print('PASS: passive phase ordering, unchanged original invocation, callback drain, read-only lease accessor')
