"""Bounded read-only owned pose3 + native Empty/null-slot capture. No input or writes.

Capture labels are instructions/provenance, never proof that a pose is a fist.
Run without tracked-arm animation writes; compare private mesh before certification.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import struct
import time

from inspect_native_hands import hand_state
from rig_fields import GAME_HASH, animation_snapshot, body_snapshot, pose_layout_snapshot, pointer


class EmptyWindow:
    """Conservative sample-time eligibility, matching the native policy hysteresis."""
    def __init__(self):
        self.clear()

    def clear(self):
        self.owner = None
        self.last = None
        self.since = [None, None]
        self.count = [0, 0]

    def observe(self, state, now, focused):
        if state is None or not focused or state.get('carry_consistent') is not True or state.get('native_carry',{}).get('typed_owner_verified') is not True:
            self.clear()
            return 0
        owner = (json.dumps(state['native_carry'],sort_keys=True),state['player'], state['body'], state['body_identity'], state['inventory'], state['inventory_identity'],
                 tuple((h['animation'], h['animation_identity'], h['model'], h['graph_decl'], h['equip_name_list'],
                        h.get('slot'),h.get('equip_index')) for h in state['hands']))
        if owner != self.owner or self.last is None or now < self.last or now-self.last > .2:
            self.clear()
            self.owner = owner
        duplicate = self.last == now
        self.last = now
        result = 0
        for side, hand in enumerate(state['hands']):
            if not hand['empty_candidate'] or state['native_carry']['carrying']:
                self.since[side] = None
                self.count[side] = 0
                continue
            if self.since[side] is None:
                self.since[side] = now
            if not duplicate:
                self.count[side] += 1
            if self.count[side] >= 20 and now-self.since[side] >= 1.:
                result |= 1 << side
        return result


class OwnedStateWindow(EmptyWindow):
    """Diagnostic source stability only; occupied state never establishes empty eligibility."""
    def observe(self,state,now,focused):
        if state is not None:
            state=dict(state,hands=[dict(hand,empty_candidate=True) for hand in state['hands']])
        return super().observe(state,now,focused)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--report', type=Path, default=Path('artifacts/game-probe-rig.json'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=8.)
    parser.add_argument('--label', required=True, help='Requested native action, not a certified pose name')
    parser.add_argument('--include-occupied',action='store_true',help='Diagnostic authored grasp capture; never enables empty-hand eligibility')
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    if args.pid <= 0 or not 0 < args.seconds <= 15 or c.sizeof(c.c_void_p) != 8 or not output.is_relative_to(workspace):
        parser.error('Require x64, positive PID, 0..15 seconds and workspace output')
    metadata = json.loads(args.report.read_text(encoding='utf-8-sig'))['executable']
    if metadata['sha256'] != GAME_HASH:
        parser.error('Require pinned executable report')
    required_types=('arkPlayer','arkSkinnedRenderModel','arkCpntAnimEvolved','arkCpntPlayerBody')
    types = {name: metadata['engine_type_candidates'][name] for name in required_types}
    k = c.WinDLL('kernel32', use_last_error=True)
    u = c.WinDLL('user32', use_last_error=True)
    class Module(c.Structure):
        _fields_ = [('dwSize',w.DWORD),('th32ModuleID',w.DWORD),('th32ProcessID',w.DWORD),('GlblcntUsage',w.DWORD),('ProccntUsage',w.DWORD),
                    ('modBaseAddr',c.c_void_p),('modBaseSize',w.DWORD),('hModule',w.HMODULE),('szModule',w.WCHAR*256),('szExePath',w.WCHAR*260)]
    k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
    k.Module32FirstW.argtypes=[w.HANDLE,c.POINTER(Module)];k.Module32FirstW.restype=w.BOOL
    k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
    k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)];k.ReadProcessMemory.restype=w.BOOL
    k.CloseHandle.argtypes=[w.HANDLE];k.CloseHandle.restype=w.BOOL
    u.GetForegroundWindow.restype=w.HWND
    u.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)];u.GetWindowThreadProcessId.restype=w.DWORD
    snapshot=k.CreateToolhelp32Snapshot(8,args.pid)
    if snapshot==c.c_void_p(-1).value:raise OSError(c.get_last_error(),'Module snapshot failed')
    try:
        module=Module(dwSize=c.sizeof(Module))
        if not k.Module32FirstW(snapshot,c.byref(module)) or module.szModule.lower()!='dishonored2.exe' or module.modBaseSize!=0x41cb000:
            raise ValueError('Require pinned Dishonored2 image')
        if hashlib.sha256(Path(module.szExePath).read_bytes()).hexdigest()!=GAME_HASH:raise ValueError('Executable hash differs')
        base=module.modBaseAddr
    finally:k.CloseHandle(snapshot)
    process=k.OpenProcess(0x1010,False,args.pid)  # VM_READ | QUERY_LIMITED_INFORMATION only
    if not process:raise OSError(c.get_last_error(),'Read-only process open failed')
    samples=[];window=EmptyWindow();owned_window=OwnedStateWindow();start=time.monotonic();started_unix=time.time();last_pose=-1.
    output.mkdir(parents=True,exist_ok=True)
    try:
        def read(at,size):
            if not 0x10000<=at<0x0000800000000000-size or not 0<size<=32768:raise ValueError('Invalid bounded read')
            raw=c.create_string_buffer(size);got=c.c_size_t()
            if not k.ReadProcessMemory(process,at,raw,size,c.byref(got)) or got.value!=size:raise OSError(c.get_last_error(),'Native read failed')
            return raw.raw
        masks=struct.unpack('<II',read(base+0x1c70e88+2*8,8))
        while time.monotonic()-start<args.seconds:
            now=time.monotonic();foreground=w.DWORD()
            u.GetWindowThreadProcessId(u.GetForegroundWindow(),c.byref(foreground))
            focused=foreground.value==args.pid
            sample=dict(seconds=now-start,gameplay_window_focused=focused)
            try:
                state=hand_state(read,base)
                sample['state']=state
                eligible=window.observe(state,now,focused)
                sample['sustained_empty_bits']=eligible
                stable=owned_window.observe(state,now,focused)
                sample['sustained_owned_state_bits']=stable
                if (eligible or (args.include_occupied and stable)) and now-last_pose>=.2:
                    last_pose=now
                    body=body_snapshot(read,state['body'],base,types,masks,3,True)
                    native=animation_snapshot(read,pointer(read,state['body']+0x58),base,types,masks,3)
                    pose_layout_snapshot(read,native,base)
                    state_after=hand_state(read,base)
                    if state_after!=state:raise ValueError('Equip/carry/owner state changed across pose capture')
                    if any(a['skeleton_joints']!=69 for a in body['arms']):raise ValueError('Require both owned 69-joint arms')
                    report=dict(executable_sha256=GAME_HASH,pid=args.pid,game_memory_writes=False,engine_calls=False,
                        captured_unix=time.time(),
                        requested_action=args.label,authored_fist_captured=False,authored_pose_confirmed=False,
                        tracked_arm_writes_absent_verified=False,sustained_empty_bits=eligible,
                        occupied_source_capture_allowed=args.include_occupied,sustained_owned_state_bits=stable,native_hand_state=state,
                        carry_before=state['native_carry'],carry_after=state_after['native_carry'],carry_consistent=True,
                        snapshot_frame_synchronised=False,body_samples=[body],rig_samples=[native])
                    name=f'pose-{len(samples):04d}.json'
                    (output/name).write_text(json.dumps(report,indent=2)+'\n')
                    sample['pose_report']=name
            except (OSError,ValueError,KeyError,struct.error) as error:
                window.clear();owned_window.clear();sample['error']=str(error);sample['sustained_empty_bits']=0
            samples.append(sample)
            time.sleep(max(0.,.05-(time.monotonic()-now)))
    finally:k.CloseHandle(process)
    result=dict(pid=args.pid,base=hex(base),executable_sha256=GAME_HASH,requested_action=args.label,
        started_unix=started_unix,ended_unix=time.time(),
        game_memory_writes=False,engine_calls=False,authored_fist_captured=False,occupied_source_capture_allowed=args.include_occupied,samples=samples)
    (output/'capture.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(output=str(output/'capture.json'),samples=len(samples),poses=sum('pose_report'in s for s in samples),
                         errors=sum('error'in s for s in samples))))


if __name__=='__main__':main()
