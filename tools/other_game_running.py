# Used by tools/vr_selftest.py --launch (and the sim setup scripts): never start Dishonored 2 over a game the user is playing.
# Exit 0 when Steam tracks no running game other than Dishonored 2 (403640); exit 2 and name it otherwise.
import os,re,subprocess,sys
def steam_root():
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\Valve\Steam') as k: return winreg.QueryValueEx(k,'SteamPath')[0]
    except OSError: return r'C:\Program Files (x86)\Steam'
log=os.path.join(steam_root(),'logs','gameprocess_log.txt') # Steam's own install path (not always the default C: one)
running={}
for line in open(log,encoding='utf-8',errors='replace'):
    m=re.search(r'AppID (\d+) adding PID (\d+) as a tracked process "(.*)"',line)
    if m: running[m.group(2)]=(m.group(1),m.group(3));continue
    m=re.search(r'AppID (\d+) no longer tracking PID (\d+)',line)
    if m: running.pop(m.group(2),None)
alive=subprocess.run(['tasklist','/FO','CSV','/NH'],capture_output=True,text=True).stdout
# A PID Steam never saw end can be reused by any process (2026-11-10: a crash reporter's PID became ApplicationFrameHost.exe): the image name must match too.
pids={l.split('","')[1]:l.split('","')[0].strip('"').lower() for l in alive.splitlines() if l.count('","')>=1}
def image(exe):  # the first .exe file name in the tracked command line (a quoted path, or a bare one followed by arguments)
    m=re.search(r'([^\\/"]+?\.exe)',exe,re.I);return m.group(1).strip().lower() if m else ''
assert image(r'.\crs-handler.exe --x "--binary-path=C:\a\SB.exe"')=='crs-handler.exe' and image(r'"C:\Program Files (x86)\a b\Dishonored2.exe"')=='dishonored2.exe'
others=[(app,pid,exe) for pid,(app,exe) in running.items() if app!='403640' and pids.get(pid)==image(exe)]
if others:
    print('STOP: another game is running:',others);sys.exit(2)
print('no other game running')
