"""Exercise the actual Hmd2 implementation with only host runtime/tile/clock I/O substituted.

The real Windows metadata reader/writer and all snapshot/lifecycle functions are compiled.
No headset, game or Vulkan queue is used. This is not an end-to-end VR rendering test.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT.parent / 'hmd2-regression'

def block(text, marker):
    start=text.index(marker); p=text.index('{',start); depth=1; q=p+1
    while depth:
        if text[q]=='{':depth+=1
        elif text[q]=='}':depth-=1
        q+=1
    return text[start:q]

def main():
    global ROOT, OUT
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--vcvars',type=Path,required=True)
    args=parser.parse_args()
    ROOT=args.source.resolve(); OUT=(args.output or ROOT.parent/'hmd2-regression').resolve()
    COMPILER=args.compiler.resolve(); VCVARS=args.vcvars.resolve()
    OUT.mkdir(parents=True,exist_ok=True)
    paths=[ROOT/'src/libs/libHmd2.cpp', ROOT/'src/libs/hmd2.h', ROOT/'src/graphics/shader/shaderBindings.h']
    cpp,header,bindings=[p.read_text(encoding='utf-8-sig') for p in paths]
    body=re.sub(r'^#include[^\n]*\n','',cpp,flags=re.M)
    body=body.replace(block(body,'static uint64_t HmdSteadyNs()'), 'static uint64_t HmdSteadyNs() { return TestSteadyNs(); }')
    stub=r'''
#define KYTY_PLATFORM 1
#define KYTY_PLATFORM_WINDOWS 1
#define KYTY_SYSV_ABI
#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#define LIB_VERSION(...)
#define LIB_DEFINE(name) void name()
#define LIB_FUNC(...)
#define PRINT_NAME()
#define LOGF(...)
#define EXIT_IF(x) do {if(x)throw std::runtime_error("EXIT_IF");}while(0)
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
uint64_t fake_monotonic_ns=1000000000, fake_clock_step=10000;
uint64_t steady_calls=0,guest_clock_calls=0;
uint64_t TestSteadyNs(){++steady_calls;auto v=fake_monotonic_ns;fake_monotonic_ns+=fake_clock_step;return v;}
namespace fmt {
template<class... A> std::string sprintf(const char* format,A... args){
 std::array<char,4096> out{};int n=std::snprintf(out.data(),out.size(),format,args...);
 if(n<0||static_cast<size_t>(n)>=out.size())throw std::runtime_error("diagnostic format overflow");
 return std::string(out.data(),static_cast<size_t>(n));
}
}
namespace Log {
std::vector<std::string> lines;std::function<void()> hook;
void WriteToConsoleAndLog(std::string_view text){lines.emplace_back(text);if(hook)hook();}
}
namespace Config { bool enabled=true; bool VrEnabled(){return enabled;} }
namespace Libs {
constexpr int OK=0;
namespace VideoOut {
constexpr int VIDEO_OUT_ERROR_INVALID_VALUE=-1001,VIDEO_OUT_ERROR_NO_DEVICE=-1002,
 VIDEO_OUT_ERROR_INVALID_HANDLE=-1003,VIDEO_OUT_ERROR_INVALID_INDEX=-1004;
}
namespace LibKernel { uint64_t clock=1000000,step=100; uint64_t KernelGetProcessTime(){++guest_clock_calls;auto r=clock;clock+=step;return r;} }
namespace Graphics {
namespace Prospero {
enum class ImageType:uint32_t{kColor2DArray=13};
enum class BufferFormat:uint32_t{k10_10_10_2UNorm=1};
enum class TileMode:uint32_t{kRenderTarget=0};
}
@TEXTURE@;
struct SizeAlign {uint64_t m_size;size_t m_align;};
struct TileSizeAlign {uint64_t size=0;size_t align=0;};
void TileGetTextureTotalSize(Prospero::BufferFormat,uint32_t w,uint32_t h,int,int,Prospero::TileMode,bool,TileSizeAlign& o){o={uint64_t(w)*h*4,256};}
namespace Vr {
struct Status {bool ok=false;std::string message;explicit operator bool()const{return ok;}};
struct EyeGeometry {float left=-1,right=1,top=-1,bottom=1;};
struct HeadsetInfo {bool connected=true;uint32_t recommended_eye_width=2000,recommended_eye_height=2000;bool ipd_known=true;float ipd_m=.064f;int activity_level=1;std::array<EyeGeometry,2> eyes;};
struct DisplayTiming {float prediction_seconds=.01f;uint64_t sample_monotonic_ns=0;
 float frequency_hz=90,seconds_since_last_vsync=0,seconds_from_vsync_to_photons=.01f;uint64_t vsync_counter=123;};
struct Bridge {bool available=true;HeadsetInfo info;DisplayTiming timing;std::function<void()> hook;
 unsigned timing_reads=0;std::function<void(unsigned)> query_hook;
 bool force_sample=false;uint64_t sample=0;
 bool ReadHeadset(HeadsetInfo& o){o=available?info:HeadsetInfo{};return available;}
 Status ReadDisplayTiming(DisplayTiming& o){++timing_reads;if(query_hook)query_hook(timing_reads);if(hook)std::exchange(hook,{})();o=timing;o.sample_monotonic_ns=TestSteadyNs();if(force_sample)o.sample_monotonic_ns=sample;return {available,available?"":"test host timing unavailable"};}
};
Bridge& HostBridge(){static Bridge b;return b;}
}
}
}
@HEADER@
'''
    stub=stub.replace('@TEXTURE@',block(bindings,'struct ShaderTextureResource {'))
    stub=stub.replace('@HEADER@',block(header,'namespace Libs::Hmd2 {'))
    tests=Path(__file__).with_name('Hmd2BridgeRegression.cpp.in').read_text()
    source=stub+'\n'+body+'\n'+tests
    variants={'fixed':source,
      'negative_raw_fov_vertical_names':source.replace('eye.right, eye.bottom, -eye.top', 'eye.right, -eye.top, eye.bottom'),
      'negative_wrong_layer_offset':source.replace('layer.data() + 8, sizeof(snapshot.eyes)', 'layer.data() + 4, sizeof(snapshot.eyes)'),
      'negative_repeat_consumption':source.replace('out = slot.snapshot;\n\tslot.ready = false;', 'out = slot.snapshot;'),
      'negative_stale_on_invalid':source.replace('slot.ready = false;\n\t\tslot.ticket = ticket;', 'slot.ticket = ticket;'),
      'negative_diagnostic_budget':source.replace('g_layer_diagnostic_records >= 32', 'g_layer_diagnostic_records >= 33'),
      'negative_diagnostic_offsets':source.replace('word(0x128), word(0x12c)', 'word(0x120), word(0x12c)'),
      'negative_timing_reason':source.replace('TimingReason::SampleExpired, VideoOut::VIDEO_OUT_ERROR_NO_DEVICE', 'TimingReason::SampleFuture, VideoOut::VIDEO_OUT_ERROR_NO_DEVICE'),
      'negative_timing_budget':source.replace('if (occurrence > 4)', 'if (occurrence > 5)'),
      'negative_timing_no_retry':source.replace('for (bool may_retry = true;; may_retry = false)', 'for (bool may_retry = false;; may_retry = false)'),
      'negative_timing_unbounded_retry':source.replace('for (bool may_retry = true;; may_retry = false)', 'for (bool may_retry = true;; may_retry = true)'),
      'negative_timing_skip_second_read':source.replace('const auto timing_status = Graphics::Vr::HostBridge().ReadDisplayTiming(timing);', 'const auto timing_status = may_retry ? Graphics::Vr::HostBridge().ReadDisplayTiming(timing) : Graphics::Vr::Status{false, "skipped fresh sample"};'),
      'negative_timing_disabled':source.replace('if (!HmdLayerDiagnosticsEnabled()) {\n\t\treturn result;', 'if (false) {\n\t\treturn result;')}
    results=[]
    for name,text in variants.items():
        p=OUT/(name+'.cpp');p.write_text(text)
        exe=OUT/(name+'.exe')
        command=f'"{COMPILER}" /nologo /std:c++20 /EHsc /Od /W4 /Fe:"{exe}" /Fo:"{OUT/(name+".obj")}" "{p}"'
        bat=OUT/(name+'.cmd');bat.write_text(f'@echo off\ncall "{VCVARS}" >nul\n{command}\nexit /b %errorlevel%\n')
        c=subprocess.run(['cmd','/d','/c',str(bat)],capture_output=True,text=True)
        (OUT/(name+'-compile.log')).write_text(c.stdout+c.stderr)
        for mode in (['unset','0','1'] if name=='fixed' else ['0'] if name=='negative_timing_disabled' else ['1']):
            env=os.environ.copy()
            if mode=='unset':env.pop('KYTY_VR_DIAGNOSTICS',None)
            else:env['KYTY_VR_DIAGNOSTICS']=mode
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30,env=env) if c.returncode==0 else None
            if r:(OUT/(name+'-'+mode+'-run.log')).write_text(r.stdout+r.stderr)
            results.append({'variant':name,'diagnostics_env':mode,'compile_exit':c.returncode,'actual_exit':r.returncode if r else None,
                            'expected_exit':0 if name=='fixed' else 1,'passed':c.returncode==0 and r is not None and r.returncode==(0 if name=='fixed' else 1)})
            print(name,results[-1],r.stdout if r else c.stdout+c.stderr)
    report={'passed':all(r['passed'] for r in results),'source_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
      'test_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__),Path(__file__).with_name('Hmd2BridgeRegression.cpp.in')]},
      'results':results,'scope':'Actual whole Hmd2 source (includes removed), actual descriptor/header types, real Windows read/write and actual getenv opt-in. Runtime, tile allocator, clock and log sink/formatter are test doubles. Checks one-query success, fresh second runtime sample after host failure/expiry/clock interruption, max two attempts, no stale-token publication, epoch change during retry, exact target anchoring, disabled no-clock behavior, raw offsets, throttling, 32-record layer budget, all12 timing rejection causes, per-reason 4-line process budget, unchanged errors/output sentinel, unlocked diagnostics, logging exception preservation and one-shot publication. No GPU/headset/game run; does not establish stereo rendering, repair of observed pitch distortion, the cause of earlier unknown timing records, or SDK-wide ABI compatibility.'}
    (OUT/'results.json').write_text(json.dumps(report,indent=2))
    raise SystemExit(0 if report['passed'] else 1)
if __name__=='__main__':main()
