from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description='Compile actual tracker code with host I/O doubles. Does not start SteamVR or a game.')
parser.add_argument('--source',type=Path,default=ROOT)
parser.add_argument('--output',type=Path)
parser.add_argument('--compiler',type=Path,required=True)
parser.add_argument('--vcvars',type=Path,required=True)
args=parser.parse_args()
ROOT=args.source.resolve(); OUT=(args.output or ROOT.parent/'tracker-regression').resolve();OUT.mkdir(parents=True,exist_ok=True)
loaded=subprocess.run(f'cmd.exe /d /s /c "call "{args.vcvars}" >nul && set"',text=True,capture_output=True,check=True)
env=os.environ.copy();env.update(line.split('=',1) for line in loaded.stdout.splitlines() if '=' in line and not line.startswith('='))
overlay=OUT/'include'
stubs={
 'common/emulatorConfig.h':'#pragma once\nnamespace Config { inline bool VrEnabled(){return true;} }\n',
 'kernel/pthread.h':'#pragma once\ninline uint64_t now_us=1000000; namespace LibKernel { inline uint64_t KernelGetProcessTime(){return now_us;} }\n',
 'loader/symbolDatabase.h':'#pragma once\nnamespace Loader { class SymbolDatabase {}; }\n',
 'libs/libs.h':'#pragma once\n#define KYTY_SYSV_ABI __attribute__((sysv_abi))\n#define LIB_VERSION(...)\n#define LIB_DEFINE(name) void name(Loader::SymbolDatabase*)\n#define LIB_FUNC(...)\n',
 'graphics/vr/openvrBridge.h':'''#pragma once
#include <array>
namespace Libs::Graphics::Vr {
struct Status { bool ok; explicit operator bool()const{return ok;} };
struct Pose {float device_to_absolute[3][4]{};bool connected=false,valid=false;};
struct EyeGeometry{float eye_to_head[3][4]{};};
struct HeadsetInfo{bool connected=false;std::array<EyeGeometry,2> eyes;};
inline Pose fake_pose;inline HeadsetInfo fake_headset;inline bool host_ok=true;inline float last_prediction=0;
inline unsigned headset_calls=0,tracking_calls=0;
class FakeBridge {public:Status ReadHeadset(HeadsetInfo& h){++headset_calls;h=fake_headset;return{host_ok};}
 Status ReadTrackingSample(float s,Pose& p,std::array<EyeGeometry,2>& eyes){++tracking_calls;last_prediction=s;p=fake_pose;eyes=fake_headset.eyes;return{host_ok&&fake_headset.connected};}};
inline FakeBridge& HostBridge(){static FakeBridge bridge;return bridge;}
}
'''}
for relative,content in stubs.items():
 p=overlay/relative;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(content)
production=ROOT/'src/libs/libVrTracker2.cpp'
tu=OUT/'tracker-test.cpp'
tu.write_text((ROOT/'tests/VrTracker2Regression.cpp.in').read_text().replace('@PRODUCTION_CPP@',production.as_posix()))
exe=OUT/'tracker-test.exe';log=OUT/'compile.log'
def compile_test(source,output):
 return subprocess.run([str(args.compiler),'/nologo','/std:c++20','/EHsc','/O1','/DWIN32_LEAN_AND_MEAN','/I'+str(overlay),'/I'+str(ROOT/'src'),str(source),'/Fe:'+str(output),'/Fo:'+str(output.with_suffix('.obj')),'/link','/INCREMENTAL:NO'],cwd=OUT,env=env,capture_output=True,text=True)
build=compile_test(tu,exe)
log.write_text(build.stdout+build.stderr)
result={'build_exit':build.returncode,'fixture':'Direct include of actual libVrTracker2.cpp; only host bridge, guest clock and registration dependencies replaced. Win32 guest I/O is real, no game or SteamVR session started.','source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [production,ROOT/'src/libs/libVrTracker2.h',ROOT/'tests/VrTracker2Regression.cpp.in']}}
if build.returncode==0:
 run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
 (OUT/'run.log').write_text(run.stdout+run.stderr)
 result.update(actual_exit=run.returncode,stdout=run.stdout,passed=run.returncode==0,exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
else:result['passed']=False
if result['passed']:
 controls=[]
 original=production.read_text()
 replacements=[
  ('duplicate-left-eye','eye_geometry[1].eye_to_head','eye_geometry[0].eye_to_head','actual eye geometry'),
  ('stale-render-pose','!CopyToGuest(OutputField(output, 0x70), invalid_orientation.data(), sizeof(invalid_orientation))','false','failed request cannot replay')]
 for label,old,new,expected in replacements:
  assert original.count(old)==1
  altered=OUT/(label+'.cpp');altered.write_text(original.replace(old,new))
  mutant_tu=OUT/(label+'-test.cpp');mutant_tu.write_text(tu.read_text().replace(production.as_posix(),altered.as_posix()))
  mutant_exe=OUT/(label+'.exe')
  compiled=compile_test(mutant_tu,mutant_exe)
  (OUT/(label+'-compile.log')).write_text(compiled.stdout+compiled.stderr)
  control={'name':label,'compile_exit':compiled.returncode,'expected_exit':1,'expected_reason':expected}
  if compiled.returncode==0:
   ran=subprocess.run([str(mutant_exe)],capture_output=True,text=True,timeout=30)
   (OUT/(label+'-run.log')).write_text(ran.stdout+ran.stderr)
   control.update(actual_exit=ran.returncode,reason=ran.stderr.splitlines()[-1],passed=ran.returncode==1 and expected in ran.stderr)
  else:control['passed']=False
  controls.append(control)
 result['mutation_controls']=controls
 result['passed'] &= all(c['passed'] for c in controls)
(OUT/'results.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
raise SystemExit(0 if result['passed'] else 1)
