"""Independent offline dry weapon lease audit; never enables output writers."""
import csv,json,hashlib,zipfile,collections,argparse
from pathlib import Path


def validate_typed_rows(dry,diag):
    """Separate accepted typed leases from rejected equipment-state diagnostics."""
    accepted=[row for row in dry if row['accepted']=='1']
    rejected=[row for row in dry if row['accepted']!='1']
    allowed_reasons={'10','11','12'}
    immutable=(('receipt_serial','native_frame','side','frozen_revision','posed69_digest',
                'equipment','equipment_generation','equipment_declaration','equipment_item')+
               tuple('predicted_socket'+str(i) for i in range(16)))
    groups=collections.defaultdict(list);failed_groups=collections.defaultdict(list)
    for row in accepted:
        token=int(row['token']);assert token>0 and row['reason']=='0'
        assert row['typed_equipment_verified']=='1'
        assert row['strict_revision']=='1' and row['outputs_changed']=='false'
        assert int(row['equipment'])>0 and int(row['equipment_declaration'])>0 and int(row['equipment_item'])>0
        assert int(row['equipment_generation'])>0
        groups[token].append(row)
    transition_seen={}
    for row in dry:
        if row['accepted']=='1':continue
        assert row['accepted']=='0'
        assert row['outputs_changed']=='false'
        token=int(row['token'])
        if token==0:
            # A tokenless absent/changed/link-mismatch diagnostic never enters
            # prepared/completed lease counts.
            assert row['reason'] in allowed_reasons
            assert row['typed_equipment_verified']=='0'
        else:
            # reason 2 is the explicit lease clear/revocation transition;
            # 10/11/12 are typed-equipment absence/change/link-mismatch.
            if row['reason'] in allowed_reasons|{'2'}:
                transition_seen.setdefault(str(token),row['reason'])
            else:
                # Once a lease is invalidated, later callback observations can
                # report absent (1); keep those as transition rows, never leases.
                assert row['reason']=='1' and str(token) in transition_seen
            failed_groups[token].append(row)
    for token,transaction in groups.items():
        sites=[int(row['site']) for row in transaction]
        assert sites==[site for site in (0,1,4,5) if site in sites]
        assert len(sites)==len(set(sites))
        first=transaction[0]
        assert all(all(row[key]==first[key] for key in immutable) for row in transaction)
        assert all(row['strict_revision']=='1' and row['outputs_changed']=='false' for row in transaction)
        expected_after=['0','1','1','1'][:len(transaction)]
        expected_complete=['0','0','0','1'][:len(transaction)]
        assert [row['after_hypothetical_primary'] for row in transaction]==expected_after
        assert [row['observed_consumers_complete'] for row in transaction]==expected_complete
    for token,failures in failed_groups.items():
        assert token in groups
        lease=groups[token][0]
        for row in failures:
            # Failures may clear verification status, but they must still refer
            # to the same typed lease identity and must not be counted accepted.
            for key in ('receipt_serial','native_frame','side','frozen_revision','posed69_digest',
                        'equipment','equipment_generation','equipment_declaration','equipment_item'):
                if row[key] not in ('','0'):assert row[key]==lease[key]
            assert row['typed_equipment_verified']=='0'
    complete=[token for token,transaction in groups.items()
              if [int(row['site']) for row in transaction]==[0,1,4,5] and token not in failed_groups]
    assert len(groups)==diag['native_weapon_dry_prepared']
    assert len(complete)==diag['native_weapon_dry_completed']
    assert diag['native_weapon_dry_revoked']<=diag['native_weapon_dry_prepared']
    # Tokenless diagnostics are deliberately reported separately and never
    # inflate transaction or completion totals.
    revoked_tokens={int(row['token']) for row in rejected if row['token']!='0' and row['reason']=='2'}
    assert len(revoked_tokens)==diag['native_weapon_dry_revoked']
    return groups,accepted,rejected,complete


def audit_typed_equipment(run):
    """Audit v98 typed-equipment rows, including expected rejection records."""
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    load=lambda name:json.loads((run/name).read_text(encoding='utf-8-sig'))
    rows=lambda name:list(csv.DictReader((run/name).open(newline='',encoding='utf-8-sig')))
    provenance=load('build-source-provenance.json');manifest=load('build-source-manifest.json');diag=load('dxgi-probe-result.json')
    launch=load('probe-launch-config.json')
    assert provenance['source_unchanged_during_build'] and provenance['state']=='complete'
    assert sha(run/'build-source.zip')==provenance['source_zip_sha256']
    assert sha(run/'build-source-manifest.json')==provenance['source_manifest_sha256']
    loaded=next(run.glob('loaded-render-probe-*.json'))
    assert json.loads(loaded.read_text())['sha256']==provenance['dll_sha256']
    assert launch['status']=='completed' and launch['diagnostic_exit']==0
    assert launch['pid']==diag['pid'] and launch['dll_sha256']==provenance['dll_sha256']
    assert launch['executable_sha256']=='b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
    assert launch['dll_name']==provenance['dll_name']=='d2vr_render_probe_arm_v98.dll'
    assert launch['tracked_arms_requested'] is True and launch['arm_palette_copy_requested'] is False
    assert provenance['dll_sha256']=='d20dc74b66179a5353c9494b3e4e802a587a41016623b79d5bf4d58d8f91c615'
    sources={}
    with zipfile.ZipFile(run/'build-source.zip') as archive:
        for name in ('src/native_weapon_dry_lease.hpp','src/native_weapon_source_receipts.hpp','src/native_weapon_attachment_candidate.hpp','src/native_weapon_attachment_trace.hpp','src/native_equipment_ownership.hpp'):
            raw=archive.read('source/'+name);item=next(item for item in manifest['files'] if item['path']==name)
            assert hashlib.sha256(raw).hexdigest()==item['sha256'];sources[name]=item['sha256']
    dry=rows('native-weapon-dry-lease.csv');models=rows('native-weapon-model-base.csv');attachments=rows('native-weapon-attachment.csv')
    groups,accepted,rejected,complete=validate_typed_rows(dry,diag)
    modelkeys=collections.defaultdict(list)
    for row in models:modelkeys[(row['receipt_serial'],row['native_frame'],row['side'])].append(row)
    attachmentkeys=collections.defaultdict(list)
    for row in attachments:attachmentkeys[(row['receipt_serial'],row['native_frame'],row['side'])].append(row)
    full_owner_fields=('player','body','body_generation','animation','animation_generation','skeleton','model')
    joined=0;missing_model_observations=0;gpu_model_joins=0;model_keys_without_gpu=0;owner_sets=set();max_latency=0
    source_joined=0
    for token,transaction in groups.items():
        first=transaction[0];max_latency=max(max_latency,int(transaction[-1]['tick'])-int(first['tick']))
        key=(first['receipt_serial'],first['native_frame'],first['side'])
        source=attachmentkeys[key]
        assert len(source)==1
        assert source[0]['receipt_matched']=='true' and source[0]['receipt_reason']=='0'
        assert source[0]['receipt_frame']==first['native_frame'] and source[0]['receipt_serial']==first['receipt_serial']
        source_joined+=1
        observations=modelkeys[key]
        if not observations:
            missing_model_observations+=1
            continue
        assert all(model['source_matched']=='1' and model['source_reason']=='0' and model['receipt_frame']==first['native_frame'] for model in observations)
        assert all(model['tracking_revision']==first['frozen_revision'] and model['tracking_calibrated']=='1' for model in observations)
        owners={tuple(model[k] for k in full_owner_fields) for model in observations}
        assert len(owners)==1
        owner_sets.update(owners);joined+=1
        gpu_rows=[model for model in observations if model['site']=='5']
        if gpu_rows:
            assert len(gpu_rows)==1 and gpu_rows[0]['caller_rva']==str(0x541310)
            gpu_model_joins+=1
        else:model_keys_without_gpu+=1
    fits=rows('avatar-fit-events.csv');accepted_fits=[row for row in fits if row['reason']=='fit_accepted']
    reused_fits=[row for row in fits if row['reason']=='fit_reused']
    fit_tick=int(accepted_fits[0]['tick']) if accepted_fits else None
    if groups:
        assert len(accepted_fits)==1 and accepted_fits[0]['accepted']=='1' and accepted_fits[0]['template_verified']=='1'
        assert fit_tick<min(int(row['tick']) for row in accepted if row['site']=='0')
        assert diag['avatar_fit_accepts']==1 and diag['avatar_fit_reuses']==len(reused_fits)==1
        assert reused_fits[0]['reference_epoch']==accepted_fits[0]['reference_epoch']
        assert reused_fits[0]['accepted_epoch']==accepted_fits[0]['accepted_epoch']
        assert reused_fits[0]['accepted']=='1' and reused_fits[0]['active']=='1'
        assert diag['avatar_pose_gate_at_stop'] is True and diag['avatar_fit_active_at_stop'] is True
    all_equipment_reasons=collections.Counter(row['reason'] for row in rejected if row['reason'] in {'10','11','12'})
    tokenless_reasons=collections.Counter(row['reason'] for row in rejected if row['token']=='0')
    for reason,counter in (('10','native_weapon_equipment_absent'),('11','native_weapon_equipment_changed'),('12','native_weapon_equipment_mismatch')):
        assert diag[counter]>=all_equipment_reasons.get(reason,0)
    failure_first={}
    for row in rejected:
        if row['token']!='0':failure_first.setdefault(row['token'],row)
    before_failures=sum(row['after_hypothetical_primary']=='0' for row in failure_first.values())
    after_failures=sum(row['after_hypothetical_primary']=='1' for row in failure_first.values())
    assert diag['native_weapon_dry_before_primary_failures']==before_failures
    assert diag['native_weapon_dry_after_primary_failures']==after_failures
    for key in ('native_weapon_dry_secondary_matches','first_chance_fault_records','native_weapon_attachment_errors','native_weapon_source_errors'):
        assert diag[key]==0
    for key,value in diag.items():
        if key.endswith(('_restored','_hook_disabled')) or key in ('hook_disabled','gpu_hooks_disabled','xr_render_thread_shutdown'):
            assert value is True,(key,value)
    for key in ('native_weapon_dry_outputs_changed','native_weapon_attachment_writes','callback_failed','animation_writes'):
        assert diag[key] is False
    for key in ('native_weapon_attachment_trace_disabled','hook_disabled','arm_upload_hook_disabled','gpu_hooks_disabled','native_interaction_hook_disabled','values_restored','mode_restored'):
        assert diag[key] is True
    ownership_path=run.parent.parent/'research/native-arm-assets/equipment-sword-cpp-v98-44016-20261009.json'
    ownership=json.loads(ownership_path.read_text(encoding='utf-8-sig'))
    assert ownership['pid']==diag['pid'] and ownership['typed_sword_ownership'] is True
    assert ownership['game_memory_writes'] is False and ownership['engine_calls'] is False
    assert ownership['writer_eligibility'] is False
    ownership_tuple=(str(ownership['equipment']),str(ownership['equipment_generation']),str(ownership['equipment_declaration']),str(ownership['item']))
    equipment_tuples={(transaction[0]['equipment'],transaction[0]['equipment_generation'],transaction[0]['equipment_declaration'],transaction[0]['equipment_item']) for transaction in groups.values()}
    owned_tokens={token for token,transaction in groups.items()
                  if (transaction[0]['equipment'],transaction[0]['equipment_generation'],transaction[0]['equipment_declaration'],transaction[0]['equipment_item'])==ownership_tuple}
    assert owned_tokens
    assert all(all((row['equipment'],row['equipment_generation'],row['equipment_declaration'],row['equipment_item'])==ownership_tuple for row in groups[token]) for token in owned_tokens)
    hashes={name:sha(run/name) for name in ('native-weapon-dry-lease.csv','native-weapon-model-base.csv','native-weapon-attachment.csv','avatar-fit-events.csv','dxgi-probe-result.json','build-source-provenance.json','build-source-manifest.json','build-source.zip')}
    return dict(mode='typed-equipment',run=str(run),sha256={**hashes,'probe-launch-config.json':sha(run/'probe-launch-config.json'),
        'loaded_dll_identity':sha(loaded),'typed_inventory_capture':sha(ownership_path)},archived_source_sha256=sources,
        launcher={'status':launch['status'],'diagnostic_exit':launch['diagnostic_exit'],'pid':launch['pid'],
            'dll_sha256':launch['dll_sha256'],'tracked_arms_requested':launch['tracked_arms_requested'],
            'arm_palette_copy_requested':launch['arm_palette_copy_requested']},
        source_provenance={'source_unchanged_during_build':provenance['source_unchanged_during_build'],
            'compiled_from_frozen_snapshot':provenance['compiled_from_frozen_snapshot'],
            'source_manifest_sha256':provenance['source_manifest_sha256'],'source_zip_sha256':provenance['source_zip_sha256']},
        accepted_tokens=len(groups),complete_transactions=len(complete),accepted_rows=len(accepted),
        rejected_diagnostic_rows=len(rejected),tokenless_reason_counts=dict(collections.Counter(row['reason'] for row in rejected if row['token']=='0')),
        equipment_transition_counters={key:diag[key] for key in ('native_weapon_equipment_absent','native_weapon_equipment_changed','native_weapon_equipment_mismatch','native_weapon_equipment_refreshes')},
        before_primary_failures=diag['native_weapon_dry_before_primary_failures'],
        after_primary_failures=diag['native_weapon_dry_after_primary_failures'],
        sites=dict(collections.Counter(row['site'] for row in accepted)),strict_revision_accepted_rows=sum(row['strict_revision']=='1' for row in accepted),
        verified_lease_fields_immutable_across_accepted_transactions=True,source_receipt_joins=source_joined,
        model_owner_joins=joined,gpu_model_joins=gpu_model_joins,model_keys_without_gpu_row=model_keys_without_gpu,
        transactions_without_bounded_model_observation=missing_model_observations,
        distinct_typed_equipment_tuples=len(equipment_tuples),
        distinct_owner_tuples=len(owner_sets),maximum_transaction_latency_ms=max_latency,
        fit_accepted_tick=fit_tick,prepared=diag['native_weapon_dry_prepared'],completed=diag['native_weapon_dry_completed'],
        revoked=diag['native_weapon_dry_revoked'],dry_outputs_changed=False,
        native_weapon_attachment_writes_observed=diag['native_weapon_attachment_writes'],
        typed_inventory_capture={'path':str(ownership_path),'sha256':sha(ownership_path),'guard_count':ownership['guard_count'],
            'equipment_tuple':list(ownership_tuple),'matching_completed_tokens':len(owned_tokens),
            'other_completed_tokens_not_covered':len(complete)-len(owned_tokens)},
        model_observation_rows=len(models),model_observation_rows_counter=diag['native_weapon_model_observation_rows'],
        model_gpu_observations_counter=diag['native_weapon_model_gpu_observations'],
        restoration_diagnostics_pass=True,limits=['Rejected token-0 reasons 10/11/12 are diagnostics, not completed leases.',
        'Positive-token rejected rows describe invalidation/transition and are excluded from accepted transaction counts.',
        'Predicted sockets remain diagnostic; no headset alignment, comfort or native writer safety certification.'])


def audit(run,typed_equipment=False):
    if typed_equipment:return audit_typed_equipment(run)
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    load=lambda name:json.loads((run/name).read_text(encoding='utf-8-sig'))
    rows=lambda name:list(csv.DictReader((run/name).open(newline='',encoding='utf-8-sig')))
    provenance=load('build-source-provenance.json');manifest=load('build-source-manifest.json');diag=load('dxgi-probe-result.json')
    assert provenance['source_unchanged_during_build'] and provenance['state']=='complete'
    assert sha(run/'build-source.zip')==provenance['source_zip_sha256']
    assert sha(run/'build-source-manifest.json')==provenance['source_manifest_sha256']
    loaded=next(run.glob('loaded-render-probe-*.json'))
    assert json.loads(loaded.read_text())['sha256']==provenance['dll_sha256']
    sources={}
    with zipfile.ZipFile(run/'build-source.zip') as archive:
        for name in ('src/native_weapon_dry_lease.hpp','src/native_weapon_source_receipts.hpp','src/native_weapon_attachment_candidate.hpp','src/native_weapon_attachment_trace.hpp'):
            raw=archive.read("source/"+name);item=next(item for item in manifest['files'] if item['path']==name)
            assert hashlib.sha256(raw).hexdigest()==item['sha256'];sources[name]=item['sha256']
    dry=rows('native-weapon-dry-lease.csv');models=rows('native-weapon-model-base.csv');fits=rows('avatar-fit-events.csv')
    groups=collections.defaultdict(list)
    for row in dry:groups[int(row['token'])].append(row)
    immutable=('receipt_serial','native_frame','side','frozen_revision','posed69_digest')+tuple('predicted_socket'+str(i) for i in range(16))
    modelkeys=collections.defaultdict(list)
    for row in models:modelkeys[(row['receipt_serial'],row['native_frame'],row['side'],row['site'])].append(row)
    full_owner_fields=('player','body','body_generation','animation','animation_generation','skeleton','model')
    joined=0;owner_sets=set();max_latency=0
    for token,transaction in groups.items():
        assert token>0 and [int(row['site']) for row in transaction]==[0,1,4,5]
        first=transaction[0]
        assert all(all(row[k]==first[k] for k in immutable) for row in transaction)
        assert all(row['accepted']=='1' and row['reason']=='0' and row['strict_revision']=='1' and row['outputs_changed']=='false' and row['secondary_observed']=='0' for row in transaction)
        assert [row['after_hypothetical_primary'] for row in transaction]==['0','1','1','1']
        assert [row['observed_consumers_complete'] for row in transaction]==['0','0','0','1']
        assert len({row['binding'] for row in transaction[:3]})==1 and transaction[3]['binding']=='0'
        max_latency=max(max_latency,int(transaction[-1]['tick'])-int(first['tick']))
        owners=[]
        for row in transaction[1:]:
            key=(row['receipt_serial'],row['native_frame'],row['side'],row['site']);matches=modelkeys[key]
            if row['site']=='5':matches=[m for m in matches if m['caller_rva']==str(0x541310)]
            assert len(matches)==1;model=matches[0]
            assert model['source_matched']=='1' and model['source_reason']=='0' and model['receipt_frame']==row['native_frame']
            assert model['tracking_revision']==row['frozen_revision'] and model['tracking_calibrated']=='1'
            assert all(model['model_base'+str(i)]==transaction_model['model_base'+str(i)] for i in range(15)) if owners else True
            if not owners:transaction_model=model
            owners.append(tuple(model[k] for k in full_owner_fields));joined+=1
        assert len(set(owners))==1;owner_sets.add(owners[0])
    accepted=[row for row in fits if row['reason']=='fit_accepted'];assert len(accepted)==1
    fit_tick=int(accepted[0]['tick']);first_tick=min(int(row['tick']) for row in dry)
    assert fit_tick<first_tick and accepted[0]['accepted']=='1' and accepted[0]['template_verified']=='1'
    assert len(groups)==diag['native_weapon_dry_prepared']==diag['native_weapon_dry_completed']
    assert len(dry)==len(groups)*4
    for key in ('native_weapon_dry_before_primary_failures','native_weapon_dry_after_primary_failures','native_weapon_dry_revoked','native_weapon_dry_secondary_matches','first_chance_fault_records','native_weapon_attachment_errors','native_weapon_source_errors'):assert diag[key]==0
    for key in ('native_weapon_dry_outputs_changed','native_weapon_attachment_writes','callback_failed','animation_writes'):assert diag[key] is False
    for key in ('native_weapon_attachment_trace_disabled','hook_disabled','arm_upload_hook_disabled','gpu_hooks_disabled','native_interaction_hook_disabled','values_restored','mode_restored'):assert diag[key] is True
    hashes={name:sha(run/name) for name in ('native-weapon-dry-lease.csv','native-weapon-model-base.csv','avatar-fit-events.csv','dxgi-probe-result.json','build-source-provenance.json','build-source-manifest.json','build-source.zip')}
    return dict(run=str(run),sha256=hashes,archived_source_sha256=sources,transactions=len(groups),rows=len(dry),sites=dict(collections.Counter(row['site'] for row in dry)),strict_revision_rows=len(dry),immutable_logged_keys_and_socket_equal=len(groups),model_owner_joins=joined,distinct_owner_tuples=len(owner_sets),maximum_transaction_latency_ms=max_latency,fit_accepted_tick=fit_tick,first_lease_tick=first_tick,fit_lead_ms=first_tick-fit_tick,output_changes=False,writer_enabled=False,secondary_observed=False,restoration_diagnostics_pass=True,limits=['CSV retains a diagnostic digest, not all 69 joint bytes: full source/remap/list comparisons are evidenced by archived guard source and accepted runtime result, not independently reconstructed for all transactions','Predicted socket equality is within each frozen lease; no claim of equality to native rendered weapon output','Ordinary primary/GPU branch only: secondary branch not observed','No headset alignment, comfort or writer safety certification'])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--typed-equipment',action='store_true',help='Audit typed-equipment accepted leases and separate reason 10/11/12 rejects (v98 mode).');a=p.parse_args()
    if a.output.exists():raise ValueError('Refuse overwrite proof')
    result=audit(a.run,typed_equipment=a.typed_equipment);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k in ('transactions','rows','complete_transactions','accepted_tokens','accepted_rows','rejected_diagnostic_rows','model_owner_joins','fit_lead_ms','maximum_transaction_latency_ms')}))

