"""Actual FramePool/PresentVr extraction; CPU synchronization and queue command observations."""
import argparse,hashlib,json,os,pathlib,subprocess
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler',type=pathlib.Path,required=True);p.add_argument('--vcvars',type=pathlib.Path,required=True);p.add_argument('--source',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1]);p.add_argument('--output',type=pathlib.Path);a=p.parse_args()
 s=a.source.resolve();out=(a.output or s.parent/'vr-dedicated-queue-regression').resolve();out.mkdir(parents=True,exist_ok=True)
 prod=s/'src/graphics/presentation/window/swapchain.cpp';fixture=s/'tests/VrDedicatedQueueRegression.cpp.in';header=s/'src/graphics/vr/vrQueueSelection.h';text=prod.read_text();template=fixture.read_text()
 pool=text[text.index('class FramePool final {'):text.index('\nvoid Presenter::Frame::Configure(')]
 start=text.index('void Presenter::Impl::PresentVr(Frame& frame) {');prefix=text[start:text.index('\n\tstatic uint64_t accepted = 0, rejected = 0;',start)]
 method=prefix+'\n\tframes.Release(&frame);\n}\n'
 r=subprocess.run(f'cmd.exe /d /s /c "call "{a.vcvars}" >nul && set"',text=True,capture_output=True,check=True);env=os.environ.copy();env.update(x.split('=',1)for x in r.stdout.splitlines()if'='in x and not x.startswith('='))
 report={'passed':False,'source_sha256':{str(p):sha(p)for p in [prod,fixture,header,pathlib.Path(__file__)]},'cases':[],'negative_controls':[],'scope':'Verbatim FramePool and PresentVr prefix; real CPU threads. Vulkan/OpenVR/timeline signaling are controlled doubles; real GPU separate test. No headset/FPS claim.'}
 def build(name,pl,mt):
  cpp=out/(name+'.cpp');exe=out/(name+'.exe');cpp.write_text(template.replace('/*ACTUAL_FRAME_POOL*/',pl).replace('/*ACTUAL_PRESENT_PREFIX*/',mt));r=subprocess.run([str(a.compiler),'/nologo','/std:c++20','/EHsc','/MD','/O1','/I'+str(s/'src'),str(cpp),'/Fe'+str(exe)],cwd=out,env=env,capture_output=True,text=True);(out/(name+'-build.log')).write_text(r.stdout+r.stderr);return exe,{'build_exit':r.returncode,'tu_sha256':sha(cpp),'exe_sha256':sha(exe)if r.returncode==0 else None}
 def run(exe,case):
  r=subprocess.run([str(exe),case],cwd=out,env=env,capture_output=True,text=True,timeout=10);return {'case':case,'exit_code':r.returncode,'output':r.stdout+r.stderr}
 exe,built=build('cpu-current',pool,method);report['build']=built
 cases=['selection','contention','vr-retire-failure']+[prefix+case for prefix in ['', 'vr-']for case in ['normal','partial','no-work','wait-failure','inactive','missing-right','shutdown','failure','per-image']]
 if built['build_exit']==0:
  for c in cases:
   result=run(exe,c);result['passed']=result['exit_code']==0 and result['output'].strip()=='PASS '+c;report['cases'].append(result)
  mutations=[
   ('no-producer-wait',pool,method.replace('if (dedicated) { frames.WaitVrProducer(frame); }',''),'vr-normal'),
   ('shared-lock',pool,method.replace('dedicated ? graphics.vr_queue_mutex : graphics.queue_mutex','graphics.queue_mutex'),'contention'),
   ('missing-right-retirement',pool.replace('frame.vr_right->vr_present_tick = tick;',''),method,'vr-normal'),
   ('missing-reuse-wait',pool.replace('if (m_vr_master) { m_vr_master->Wait(frame.vr_present_tick); }',''),method,'vr-normal'),
   ('missing-shutdown-wait',pool.replace('m_vr_master->Wait(m_vr_master->CurrentTick() - 1);',''),method,'vr-shutdown'),
   ('success-only-retirement',pool,method.replace('if (dedicated) { frames.RetireVrPair(frame); }','if (dedicated && submitted.status) { frames.RetireVrPair(frame); }'),'vr-partial')]
  for name,pl,mt,c in mutations:
   assert pl!=pool or mt!=method
   ex,build_result=build('cpu-negative-'+name,pl,mt);item={'name':name,**build_result}
   if build_result['build_exit']==0:item.update(run(ex,c))
   item['rejected']=item.get('exit_code')==1 and item.get('output','').startswith('FAIL ');report['negative_controls'].append(item)
 report['passed']=len(report['cases'])==len(cases)and all(c['passed']for c in report['cases'])and len(report['negative_controls'])==6 and all(c['rejected']for c in report['negative_controls'])
 (out/'cpu-results.json').write_text(json.dumps(report,indent=2));print(json.dumps({'passed':report['passed'],'build':built,'cases':len(report['cases']),'negatives':len(report['negative_controls'])},indent=2));return 0 if report['passed']else 1
if __name__=='__main__':raise SystemExit(main())
