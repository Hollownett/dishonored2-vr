from pathlib import Path

root = Path(__file__).resolve().parents[1]
integration = (root / 'src/dxgi_probe.cpp').read_text(encoding='utf-8')
hooks = (root / 'src/native_assassination_receipt_hooks.hpp').read_text(encoding='utf-8')
receipt = (root / 'src/native_assassination_receipt.hpp').read_text(encoding='utf-8')
assert '#include "native_assassination_receipt.hpp"' in integration
for script in ('inject_probe.py', 'record_live_session.py'):
    assert 'native-assassination-receipts.jsonl' in (root / 'tools' / script).read_text(encoding='utf-8')
assert 'head_requested && fs::exists(root/L"build"/L"assassination-receipts")' in integration
assert 'NativeAssassinationReceipt::process_owner().flush();' in integration
assert integration.count('const bool assassination_receipt_drained=assassination_receipt_finalizer.finish();') == 2
assert integration.count('left_weapon_receipt_drained && assassination_receipt_drained) started=false;') == 2
assert 'NativeAssassinationReceiptHooks::process_owner().ready_for_install()' in integration
assert 'NativeAssassinationReceiptHooks::process_owner().stopped()' in integration
assert 'if(!NativeAssassinationReceiptHooks::process_owner().disable())' in receipt
assert receipt.index('if(!stop())return false;') < receipt.index('output_.open(')
assert receipt.index('if(!NativeAssassinationReceiptHooks::process_owner().disable())') < receipt.index('if(output_.is_open())output_.close()')
assert 'std::array<Row,256>' in receipt and 'std::array<Pending,64>' in receipt
assert 'std::try_to_lock' in receipt and '64ull*1024*1024' in receipt
assert 'native(state,&owned)' not in hooks
assert 'reinterpret_cast<Update>(native)(subject)' in hooks
assert 'reinterpret_cast<Accept>(native)(subject,packet)' in hooks
assert '0x8024d0' not in hooks
assert 'reinterpret_cast<Enter>(native)(subject,packet)' in hooks
assert 'reinterpret_cast<Arbitration>(native)(subject,state!=0)' in hooks
assert '__finally' in hooks and 'dispatch_epoch_==epoch' in hooks
assert '__finally {if(depth_<=pending_.size())pending_[depth_-1]={};--depth_;}' in receipt
for forbidden in ('TakeDamage', 'request_melee_hit', 'RequestStart', 'RequestStop', 'SetActive', 'VirtualProtect', 'MH_CreateHook'):
    assert forbidden not in receipt
assert 'accepted_copy' in receipt and 'active_victim_match' in receipt
assert 'native_result' in receipt and 'row.same_owner' in receipt
assert hooks.count('observer_failed_=true;enabled(false);') == 2
assert 'observer_failed_=false;stopping_=false;return true;' in hooks
assert 'failed_ || NativeAssassinationReceiptHooks::process_owner().observer_failed()' in receipt
assert 'enabled() && !NativeAssassinationReceiptHooks::process_owner().observer_failed()' in receipt
assert '(!assassination_receipt_requested || !d2vr::NativeAssassinationReceipt::process_owner().failed())' in integration
assert 'state==13?0xc4u:0x124u' in receipt
assert 'state==19 && value<std::uint32_t>(*selected+0x120)!=out.mode' in receipt
assert 'row.after.mode==row.packet.mode' in receipt
for source in (hooks, receipt, (root / 'tests/native_assassination_receipt_check.cpp').read_text(encoding='utf-8')):
    assert '//' not in source and '/*' not in source
print('PASS: passive assassination hook/drain/default-off/source-routing guards')
