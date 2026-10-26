"""Compile the actual bridge twice: deterministic runtime tests and a real opt-in probe.
The runner executes tests only. It does not start SteamVR, connect a headset or launch GT7.
"""
import argparse,hashlib,json,os,pathlib,subprocess,time

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1])
    p.add_argument('--output',type=pathlib.Path)
    p.add_argument('--compiler',type=pathlib.Path,required=True)
    p.add_argument('--vcvars',type=pathlib.Path,required=True)
    a=p.parse_args(); s=a.source.resolve(); out=(a.output or s.parent/'openvr-bridge-regression').resolve();out.mkdir(parents=True,exist_ok=True)
    loaded=subprocess.run(f'cmd.exe /d /s /c "call "{a.vcvars}" >nul && set"',text=True,capture_output=True,check=True)
    env=os.environ.copy();env.update(line.split('=',1) for line in loaded.stdout.splitlines() if '=' in line and not line.startswith('='))
    files=[s/'src/graphics/vr/openvrBridge.cpp',s/'src/graphics/vr/openvrBridge.h',s/'tests/OpenVrBridgeTests.cpp',s/'tests/VrRuntimeProbe.cpp',s/'3rdparty/openvr/openvr.h',s/'3rdparty/openvr/provenance.json',pathlib.Path(__file__).resolve()]
    report={'source_sha256':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},'builds':[],'passed':False,'real_runtime_probe_executed':False}
    for name in ['OpenVrBridgeTests','VrRuntimeProbe']:
        exe=out/(name+'.exe')
        cmd=[str(a.compiler),'/nologo','/std:c++20','/EHsc','/O1','/MD','/W4','/D_CRT_SECURE_NO_WARNINGS',f'/I{s/"src"}',f'/I{s/"3rdparty/Vulkan-Headers/include"}',f'/I{s/"3rdparty/nlohmann_json/include"}',str(s/'src/graphics/vr/openvrBridge.cpp'),str(s/f'tests/{name}.cpp'),f'/Fe{exe}', '/link','/incremental:no']
        t=time.monotonic();r=subprocess.run(cmd,cwd=out,env=env,text=True,capture_output=True)
        (out/(name+'-build.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
        report['builds'].append({'name':name,'exit_code':r.returncode,'seconds':time.monotonic()-t,'exe':str(exe)})
        if r.returncode:break
        report['builds'][-1]['sha256']=hashlib.sha256(exe.read_bytes()).hexdigest()
    if len(report['builds'])==2 and all(b['exit_code']==0 for b in report['builds']):
        r=subprocess.run([str(out/'OpenVrBridgeTests.exe')],cwd=out,env=env,text=True,capture_output=True,timeout=60)
        (out/'tests.log').write_text(r.stdout+r.stderr,encoding='utf-8')
        groups=[line[5:] for line in r.stdout.splitlines() if line.startswith('PASS ')]
        report.update(test_exit_code=r.returncode,groups=groups,passed=r.returncode==0 and len(groups)==25)
        diagnostic_cases=[]
        for enabled in ['0','1']:
            diagnostic_env=env.copy();diagnostic_env['KYTY_VR_DIAGNOSTICS']=enabled
            d=subprocess.run([str(out/'OpenVrBridgeTests.exe'),'--diagnostics-fixture'],cwd=out,env=diagnostic_env,text=True,capture_output=True,timeout=20)
            (out/f'diagnostics-{enabled}.log').write_text(d.stdout+d.stderr,encoding='utf-8')
            lines=[json.loads(line[len('VRSubmitMetadata '):]) for line in d.stdout.splitlines() if line.startswith('VRSubmitMetadata ')]
            summary=[line for line in d.stdout.splitlines() if line.startswith('DIAGNOSTIC_BACKEND ')]
            good=d.returncode==0 and summary==['DIAGNOSTIC_BACKEND submits=80 handoffs=40 clears=1 frames=40']
            if enabled=='0':good=good and len(lines)==0
            else:
                good=good and len(lines)==32 and [x['sequence'] for x in lines]==list(range(1,33)) and [x['token'] for x in lines]==list(range(1,33))
                good=good and lines[8]['right_accepted']==0 and lines[16]['render_pose'] is None and lines[16]['delta_known']==0
                good=good and all(x['submit_end_ns']>=x['submit_begin_ns']>0 and x['render_time_known']==0 and x['eyes'][0]['gamma']==1 and x['eyes'][1]['gamma']==0 and all(abs(a-b)<1e-6 for a,b in zip(x['eyes'][1]['bounds'],[.1,.2,.9,.8])) for x in lines)
                good=good and all(abs(x['delta_rotation_degrees']-90)<.001 and abs(x['delta_translation_metres']-14**.5)<.00001 for x in lines if x['delta_known'])
            diagnostic_cases.append({'enabled':enabled,'exit_code':d.returncode,'records':len(lines),'backend_summary':summary,'passed':good})
        report['diagnostic_cases']=diagnostic_cases;report['passed']=report['passed'] and all(c['passed'] for c in diagnostic_cases)
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2));return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
