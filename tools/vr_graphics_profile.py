"""Back up/apply a native VR graphics profile while Dishonored 2 is closed."""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
from datetime import datetime, timezone

GAME_HASH = 'b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
# Values verified against descriptions in the pinned executable. Keep the
# overall preset Custom; explicit settings preserve the other user choices.
SETTINGS = dict(r_textureDetailsPreset='3',r_modelsQualityPreset='3',r_shadowQualityPreset='3',
                r_AntiAliasingPreset='2',ark_playerBobAmount='0',view_skipShakes='1')


def edit_settings(data, desired, expected=None):
    text=data.decode('utf-8')
    newline='\r\n' if '\r\n' in text else '\n'
    lines=text.splitlines(keepends=True)
    changes, skipped={},{}
    for key,value in desired.items():
        pattern=re.compile(r'^'+re.escape(key)+r'\s+"([^"\r\n]*)"[ \t]*(?:\r?\n)?$')
        matches=[(i,pattern.fullmatch(line)) for i,line in enumerate(lines) if pattern.fullmatch(line)]
        if len(matches)>1:
            raise ValueError('Ambiguous duplicate setting: '+key)
        previous=matches[0][1].group(1) if matches else None
        if expected is not None and previous!=expected[key]:
            skipped[key]=previous
            continue
        changes[key]=dict(before=previous,after=value)
        replacement=None if value is None else f'{key} "{value}"{newline}'
        if matches:
            lines[matches[0][0]]=replacement or ''
        elif replacement:
            if lines and not lines[-1].endswith(('\n','\r')): lines[-1]+=newline
            lines.append(replacement)
    return ''.join(lines).encode('utf-8'),changes,skipped


def game_closed():
    output=subprocess.check_output(['tasklist','/FI','IMAGENAME eq Dishonored2.exe','/FO','CSV','/NH'],
                                   text=True,creationflags=subprocess.CREATE_NO_WINDOW)
    if any(row and row[0].lower()=='dishonored2.exe' for row in csv.reader(io.StringIO(output))):
        raise RuntimeError('Close Dishonored 2 before applying/restoring its startup graphics profile')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    mode=parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--apply',action='store_true')
    mode.add_argument('--restore',action='store_true')
    args=parser.parse_args()
    if os.name!='nt': raise RuntimeError('Use the Windows game host')
    root=Path(__file__).resolve().parents[1]
    config=Path.home()/'Saved Games'/'Arkane Studios'/'dishonored2'/'base'/'dishonored2Config.cfg'
    game=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2\Dishonored2.exe')
    state_path=root/'artifacts'/'vr-graphics-profile.json'
    if digest(game.read_bytes())!=GAME_HASH: raise RuntimeError('Unsupported executable')
    game_closed()
    data=config.read_bytes()
    if len(data)>1024*1024: raise ValueError('Unexpected game configuration size')
    state=json.loads(state_path.read_text()) if state_path.exists() else None
    if args.apply:
        if state and state['status']=='applied':
            raise RuntimeError('A VR graphics profile is already active; retain its baseline or restore it first')
        changed,changes,skipped=edit_settings(data,SETTINGS)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
        backup=root/'artifacts'/'settings-backups'/stamp/'dishonored2Config.cfg'
        backup.parent.mkdir(parents=True,exist_ok=False)
        backup.write_bytes(data)
        state=dict(status='applied',config=str(config),backup=str(backup),original_sha256=digest(data),
                   applied_sha256=digest(changed),changes=changes,skipped=skipped,runtime_values_verified=False)
    else:
        if not state or state['status']!='applied' or state['config']!=str(config):
            raise RuntimeError('No matching active graphics baseline')
        desired={key:value['before'] for key,value in state['changes'].items()}
        expected={key:value['after'] for key,value in state['changes'].items()}
        changed,changes,skipped=edit_settings(data,desired,expected)
        state.update(status='restored',restoration_changes=changes,restoration_skipped=skipped,
                     restored_sha256=digest(changed))
    game_closed()
    if config.read_bytes()!=data: raise RuntimeError('Configuration changed while preparing the profile')
    temporary=config.with_name(config.name+'.d2vr-tmp')
    try:
        with temporary.open('xb') as stream: stream.write(changed)
        temporary.replace(config)
    finally:
        if temporary.exists(): temporary.unlink()
    state_path.write_text(json.dumps(state,indent=2)+'\n')
    print(json.dumps(dict(status=state['status'],changes=changes,skipped=skipped,baseline=state['backup'],
                          runtime_values_verified=False)))


if __name__=='__main__':
    main()
