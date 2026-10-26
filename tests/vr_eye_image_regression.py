"""Test actual eye-description helper and production tiler/format code, without GPU/runtime.
Unused format/upload code is removed by normal /Gy and /OPT:REF; fatal sinks are test exceptions.
The sole tiler profiling macro is a no-op sink; all layout/format functions are unchanged.
"""
import argparse,hashlib,json,os,pathlib,subprocess,time
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1]);p.add_argument('--compiler',type=pathlib.Path,required=True);p.add_argument('--vcvars',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path);p.add_argument('--build',type=pathlib.Path);a=p.parse_args();s=a.source.resolve();out=(a.output or s.parent/'vr-eye-image-regression').resolve();out.mkdir(parents=True,exist_ok=True)
 loaded=subprocess.run(f'cmd.exe /d /s /c "call "{a.vcvars}" >nul && set"',text=True,capture_output=True,check=True);env=os.environ.copy();env.update(line.split('=',1)for line in loaded.stdout.splitlines()if '='in line and not line.startswith('='))
 rel=['src/graphics/vr/vrEyeImage.cpp','src/graphics/vr/vrEyeImage.h','src/graphics/guest_gpu/tile.cpp','src/graphics/guest_gpu/gpu_format.cpp','src/graphics/host_gpu/renderer/image/textureCommon.cpp','src/graphics/host_gpu/vulkanCommon.cpp','tests/VrEyeImageTests.cpp','tests/vr_eye_image_regression.py'];files=[s/x for x in rel]
 report={'passed':False,'source_sha256':{str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in files},'gpu_started':False,'cases':[]}
 includes=[s/'src',(a.build or s.parent/'build').resolve(),s/'3rdparty/Vulkan-Headers/include',s/'3rdparty/fmt/include',s/'3rdparty/tracy/public',s/'3rdparty/nlohmann_json/include',s/'3rdparty/VulkanMemoryAllocator/include']
 sources=[s/x for x in [rel[0],rel[2],rel[3],rel[4],rel[5],rel[6]]]
 shim=out/'profile-sink.h';shim.write_text('#define KYTY_COMMON_PROFILER_H_\n#define KYTY_PROFILER_FUNCTION(...) ((void)0)\n')
 base=[str(a.compiler),'/nologo','/std:c++20','/EHsc','/O1','/MD','/Gy','/Gw','/DFMT_HEADER_ONLY','/D_CRT_SECURE_NO_WARNINGS',f'/FI{shim}',*[f'/I{x}'for x in includes]]
 for name,src in [('current',sources[0]),('negative_one_layer',out/'one-layer.cpp')]:
  if name!='current':src.write_text(sources[0].read_text().replace('PhysicalLayers=2, Levels=1','PhysicalLayers=1, Levels=1'))
  cmd=base+[str(src)]+[str(x)for x in sources[1:]]+[f'/Fe{out/name}.exe','/link','/incremental:no','/OPT:REF'];t=time.monotonic();r=subprocess.run(cmd,cwd=out,env=env,text=True,capture_output=True);(out/f'{name}-build.log').write_text(r.stdout+r.stderr,encoding='utf-8');case={'name':name,'build_exit_code':r.returncode,'build_seconds':time.monotonic()-t};report['cases'].append(case)
  if r.returncode:break
  r=subprocess.run([str(out/f'{name}.exe')],cwd=out,env=env,text=True,capture_output=True,timeout=60);(out/f'{name}.log').write_text(r.stdout+r.stderr,encoding='utf-8');case.update(exit_code=r.returncode,groups=[x[5:]for x in r.stdout.splitlines()if x.startswith('PASS ')],sha256=hashlib.sha256((out/f'{name}.exe').read_bytes()).hexdigest(),output=r.stdout+r.stderr)
 report['passed']=len(report['cases'])==2 and report['cases'][0].get('exit_code')==0 and len(report['cases'][0]['groups'])==10 and report['cases'][1].get('exit_code')==1 and 'resources.layers==2' in report['cases'][1].get('output','')
 (out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2));return 0 if report['passed']else 1
if __name__=='__main__':raise SystemExit(main())
