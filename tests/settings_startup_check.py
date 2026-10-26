"""Execute the actual startup AA block; fallback must not overwrite saved intent."""
import argparse
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=root / 'src/dxgi_probe.cpp')
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8-sig')
start = source.index('        const bool alternate_taa_requested=')
start = source.index('\n', start) + 1
end = source.index('        {std::ifstream knob(root/L"build"/L"upscale")', start)
block = source[start:end]
fallback_start = source.index('        if(alternate_on() && !taa_history_installed)')
fallback = source[fallback_start:source.index('\n', fallback_start)]
prefix = r'''
#include "alternate_eye.hpp"
#include "dlss_mode.hpp"
#include <atomic>
#include <filesystem>
#include <stdexcept>
namespace fs=std::filesystem;
static bool alternate,taa_live,perf_governor_enabled;
static bool alternate_on() {return alternate;}
static std::atomic<int> temporal_live_mode;
static int fraction_mode,governor_mode;
static float aa_mode_fraction(int mode) {fraction_mode=mode;return 1.f;}
static void reset_perf_governor(int mode) {governor_mode=mode;}
static void check(bool value) {if(!value)throw std::runtime_error("startup overwrote AA preference or used requested mode as the active fallback");}
struct Fsr {bool fsr4_active{},dlss_quality{},dlss_active{},enabled{};};
int main() {
    for(bool alt:{false,true})for(bool native:{false,true})for(bool alternate_upscalers_requested:{false,true})for(int mode=0;mode<=8;++mode) {
        alternate=alt;int taa_preference=mode;
        const bool alternate_taa_requested=native;
        const fs::path root=fs::temp_directory_path()/"d2vr-settings-startup-no-knobs";
        Fsr fsr_taa;float upscale_fraction{};
'''
tail = r'''
        const int effective=d2vr::alternate_effective_temporal(alt,mode,native,alternate_upscalers_requested);
        check(taa_preference==mode && temporal_live_mode==effective);
        check(fraction_mode==effective && governor_mode==effective && upscale_fraction==1.f);
        check(fsr_taa.fsr4_active==d2vr::dlss_mode::fsr4(effective) && fsr_taa.dlss_quality==d2vr::dlss_mode::is_quality(effective));
        (void)perf_governor_enabled;taa_live=effective!=0;fsr_taa.enabled=effective>=2;
        const bool taa_history_installed=false;
'''
end_code = r'''
        check(taa_preference==mode);
        if(alt)check(temporal_live_mode==0 && !taa_live && !fsr_taa.enabled);
    }
}
'''
with tempfile.TemporaryDirectory(prefix='d2vr-settings-startup-') as directory:
    folder = Path(directory)
    cpp = folder / 'startup.cpp'
    cpp.write_text(prefix + block + tail + fallback + '\n' + end_code, encoding='utf-8')
    exe = folder / ('startup.exe' if os.name == 'nt' else 'startup')
    if os.name == 'nt':
        command = ['cl', '/nologo', '/std:c++17', '/EHsc', '/W4', '/WX',
                   '/I' + str(root / 'src'), str(cpp), '/Fo:' + str(folder / 'startup.obj'), '/Fe:' + str(exe)]
    else:
        compiler = shutil.which('g++') or shutil.which('clang++')
        if not compiler:
            raise RuntimeError('A C++17 compiler is required for the extracted production startup regression')
        command = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + str(root / 'src'), str(cpp), '-o', str(exe)]
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
    subprocess.run([str(exe)], check=True, timeout=15)
print('PASS: actual startup/fallback code preserves requested AA for all modes and render paths')
