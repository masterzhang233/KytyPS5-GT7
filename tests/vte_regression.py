"""Production VTE SPIR-V-emitter math, host viewport, stage setup, and both actual key functions."""
import argparse,hashlib,json,os,pathlib,subprocess,time

def block(text,marker):
    start=text.index(marker);begin=text.index('{',start);depth=0
    for i in range(begin,len(text)):
        if text[i]=='{':depth+=1
        elif text[i]=='}':
            depth-=1
            if depth==0:return text[start:i+1]
    raise ValueError(marker)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',type=pathlib.Path,required=True);p.add_argument('--vcvars',type=pathlib.Path);p.add_argument('--baseline-flow',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path);a=p.parse_args()
    root=pathlib.Path(__file__).resolve().parents[1];out=a.output or root.parent/'vte-regression';out.mkdir(parents=True,exist_ok=True)
    names={'helper':'graphics/guest_gpu/viewportTransform.h','shaderh':'graphics/shader/shader.h','shader':'graphics/shader/shader.cpp','disk':'graphics/host_gpu/renderer/pipeline/shaderDiskCache.cpp','pipeline':'graphics/host_gpu/renderer/pipeline/pipelineCache.cpp','render':'graphics/host_gpu/renderer/renderDraw.cpp','flow':'graphics/shader/recompiler/backend/spirv/spirvEmitterFlow.cpp'}
    paths={k:root/'src'/v for k,v in names.items()};texts={k:v.read_text(encoding='utf8') for k,v in paths.items()}
    stage=texts['pipeline'][texts['pipeline'].index('\t// VTE position formats'):texts['pipeline'].index('\n\treturn true;',texts['pipeline'].index('\t// VTE position formats'))]
    render=texts['render'];begin=render.index('\t\tconst auto& guest    = vp.viewports[i];');end=render.index('\n\t\tconst auto final_scissor',begin)
    template_path=root/'tests/VteRegression.cpp.in';template=template_path.read_text(encoding='utf8')
    call_index=texts['flow'].index('value = ConvertPositionToClipSpace(state, value);')
    call_begin=texts['flow'].rfind('\t\tif (',0,call_index)
    call_site=block(texts['flow'][call_begin:],'\t\tif (')
    chunks={'VIEWPORT_HEADER':texts['helper'],'CLIP_STRUCT':block(texts['shaderh'],'struct ShaderClipSpaceTransform')+';',
        'STATIC_KEY':block(texts['shader'],'void BuildStageStaticKey(const ShaderVertexInputInfo&'),
        'DISK_KEY':block(texts['disk'],'void AppendInputKey(ByteWriter& key, const ShaderVertexInputInfo&'),
        'STAGE_SETUP':stage,'HOST_VIEWPORT':render[begin:end],
        'EMITTER_FUNCTIONS':block(texts['flow'],'uint32_t ConvertClipCoordinate(')+'\n'+block(texts['flow'],'uint32_t ConvertPositionToClipSpace('),
        'CALL_SITE':'uint32_t value=position;\n'+call_site+'\nreturn value;'}
    text=template
    for k,v in chunks.items():text=text.replace('/*'+k+'*/',v)
    variants={'current':text,
      'negative-old-call-gate':text.replace('if (exp.kind == IR::ExportTargetKind::Position)', 'if (exp.kind == IR::ExportTargetKind::Position && state.input_info.vertex->clip_space.enabled)'),
      'negative-w-format':text.replace('(format & ViewportTransform::IncomingW) == 0','(format & ViewportTransform::IncomingW) != 0'),
      'negative-no-predivided-xy':text.replace('i < 2 ? ViewportTransform::PredividedXY : ViewportTransform::PredividedZ','i < 2 ? 0u : ViewportTransform::PredividedZ'),
      'negative-no-predivided-z':text.replace('i < 2 ? ViewportTransform::PredividedXY : ViewportTransform::PredividedZ','i < 2 ? ViewportTransform::PredividedXY : 0u'),
      'negative-offset-without-w':text.replace('biased, window, offset_w','biased, window, ConstantF32Value(state, offset)').replace('ndc, divided, w','ndc, divided, ConstantF32Value(state, 1.0f)'),
      'negative-scale-always-enabled':text.replace('return (control & (1u << (Axis * 2))) != 0 ? value : 1.0f;','return value;'),
      'negative-static-key':text.replace('key.push_back(info.clip_space.position_format);',''),
      'negative-disk-key':text.replace('key.Put(info.clip_space.position_format);',''),
      'negative-tess-stage':text.replace('vertex_info[tess_active ? 2u : 0u].clip_space','vertex_info[0].clip_space')}
    if a.baseline_flow:
        oldflow=a.baseline_flow.read_text(encoding='utf8')
        oldfunctions=block(oldflow,'uint32_t ConvertClipCoordinate(')+'\n'+block(oldflow,'uint32_t ConvertPositionToClipSpace(')
        variants['old-emitter']=text.replace(chunks['EMITTER_FUNCTIONS'],oldfunctions).replace(chunks['CALL_SITE'],'return state.input_info.vertex->clip_space.enabled ? ConvertPositionToClipSpace(state,position) : position;')
    env=os.environ.copy()
    if a.vcvars:
        loaded=subprocess.run(f'cmd.exe /d /s /c "call "{a.vcvars}" >nul && set"',capture_output=True,text=True,check=True)
        env.update(x.split('=',1) for x in loaded.stdout.splitlines() if '=' in x and not x.startswith('='))
    report={'passed':False,'source_sha256':{str(v):hashlib.sha256(v.read_bytes()).hexdigest() for v in paths.values()},'test_sha256':{str(v):hashlib.sha256(v.read_bytes()).hexdigest() for v in [template_path,pathlib.Path(__file__).resolve()]},'cases':[],
      'source':'https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/programmer-references/SI_3D_registers.pdf',
      'scope':'Actual emitted scalar SPIR-V operations executed by a small instruction evaluator, actual host viewport/stage setup and complete static/disk key functions. All 8 position formats, 64 axis enable sets, both clip ranges, clip-disable on/off and positive/negative non-unit W. No GPU/headset execution and no causal claim for the reported VR symptom.'}
    for name,code in variants.items():
        if name!='current':assert code!=text,name
        tu=out/(name+'.cpp');exe=out/(name+'.exe');tu.write_text(code,encoding='utf8');begun=time.monotonic()
        c=subprocess.run([str(a.compiler),'/nologo','/std:c++20','/EHsc','/MD','/O1',str(tu),f'/Fe{exe}','/link','/incremental:no'],cwd=out,env=env,capture_output=True,text=True)
        (out/(name+'-build.log')).write_text(c.stdout+c.stderr,encoding='utf8');r=None
        if c.returncode==0:r=subprocess.run([str(exe)],cwd=out,env=env,capture_output=True,text=True,timeout=10);(out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf8')
        expected=0 if name=='current' else 1
        report['cases'].append({'name':name,'build_exit':c.returncode,'exit_code':r.returncode if r else None,'expected_exit':expected,'passed':c.returncode==0 and r.returncode==expected,'output':(r.stdout+r.stderr).strip() if r else '', 'seconds':time.monotonic()-begun,'tu_sha256':hashlib.sha256(tu.read_bytes()).hexdigest()})
    report['passed']=all(x['passed'] for x in report['cases']);(out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps({'passed':report['passed'],'results':str(out/'results.json')},indent=2));return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
