"""Frontend instrumentation PGO, independent profiles for each source inventory."""
from support import *
import lto

def discover_pgo(pair,env,root,e):
    tools=lto.discover_tools(pair,env); major=re.search(r'clang version (\d+)',tools['compilerVersion']).group(1)
    names=['llvm-profdata.exe'] if os.name=='nt' else ['llvm-profdata-'+major,'llvm-profdata']
    candidates=[str(P(pair[1]).parent/n) for n in names]+[shutil.which(n,path=env['PATH']) for n in names]
    prof=next((x for x in candidates if x and P(x).is_file()),None)
    require(prof,'Missing matching llvm-profdata; install LLVM tools (WSL: llvm-18; Windows: Visual Studio LLVM tools or matching LLVM distribution)')
    ver=subprocess.check_output([prof,'--version'],env=env,text=True,errors='replace');require(re.search(r'version\s+'+major+r'\.',ver,re.I),'llvm-profdata major mismatch '+ver)
    tools.update(profdata=prof,profdataVersion=ver)
    if os.name=='nt':
        resource=subprocess.check_output([pair[1],'/clang:-print-resource-dir'],env=env,text=True,errors='replace').strip()
        candidates=list(P(resource).rglob('clang_rt.profile*.lib'))
        # Prefer the x64 target directory or explicitly x86_64-named runtime.
        valid=[p for p in candidates if ('x86_64' in str(p).lower() or 'x64' in str(p).lower()) and 'aarch64' not in str(p).lower() and 'arm64' not in str(p).lower()]
        require(len(valid)==1,'missing/ambiguous x64 Clang profiling runtime: '+str(candidates))
        tools['profileRuntime']=str(valid[0]);tools['resourceDir']=resource
    write(e/'pgo-tools.json',tools);return tools

def extra_flags(stage,profile=None,windows=None):
    if windows is None:windows=os.name=='nt'
    prefix='/clang:' if windows else ''
    if stage=='gen':return [prefix+'-fprofile-instr-generate']
    if stage=='pgo':
        require(profile is not None and P(profile).is_file(),'missing merged profile')
        # CMake and Ninja quoting are deliberately kept simple for the isolated work root.
        path=P(profile).resolve().as_posix();require(not re.search(r'\s',path),'PGO work path contains whitespace; use repository under a path without spaces')
        return [prefix+'-fprofile-instr-use='+path,prefix+'-Werror=profile-instr-out-of-date']
    require(stage=='full','unknown PGO stage');return []

def flags(stage,profile=None,windows=None):return ' '.join([lto.flags_for('full',windows),*extra_flags(stage,profile,windows)])
PRODUCTION_TARGETS={'chess_core','chess_evaluation','material_provider','material_evaluator','material_pst_evaluator','chess_search','chess_lab_uci'}
def disable_flag(stage,windows):
    require(stage in ('gen','pgo'),'invalid scoped stage')
    return ('/clang:' if windows else '')+'-fno-profile-instr-'+('generate' if stage=='gen' else 'use')

def cmake_flags(stage,tools,profile=None,windows=None,scope=False):
    if windows is None:windows=os.name=='nt'
    args=[x for x in lto.cmake_flags('full',tools,windows) if not re.match(r'-DCMAKE_(C|CXX)_FLAGS_RELEASE=',x)]
    args += [f'-DCMAKE_{lang}_FLAGS_RELEASE={flags(stage,profile,windows)}' for lang in ('C','CXX')]
    if windows and stage=='gen':
        runtime=P(tools['profileRuntime']); libpath=runtime.parent.as_posix()
        args += [f'-DCMAKE_{kind}_LINKER_FLAGS_RELEASE=/INCREMENTAL:NO /LIBPATH:"{libpath}" "{runtime.as_posix()}"' for kind in ('EXE','SHARED','MODULE')]
    if scope and stage!='full':
        args += [f'-DCMAKE_PROJECT_INCLUDE={(KIT / "pgo-scope.cmake").as_posix()}',f'-DCELAB_PGO_SCOPE_DISABLE={disable_flag(stage,windows)}','-DCMAKE_SUPPRESS_REGENERATION=ON']
    return args

def check_regeneration(bd,stage):
    if stage=='full':return
    cache=(bd/'CMakeCache.txt').read_text(encoding='utf-8-sig')
    require(re.search(r'^CMAKE_SUPPRESS_REGENERATION:[^=]+=(ON|TRUE|1)\s*$',cache,re.M),'automatic CMake regeneration not disabled')
    ninja=(bd/'build.ninja').read_text(encoding='utf-8-sig')
    require(not re.search(r'^build build\.ninja:.*RERUN_CMAKE',ninja,re.M),'unexpected Ninja CMake regeneration rule')

def check_compile(bd,stage,profile=None,windows=None,scope=False,required_targets=None):
    if windows is None:windows=os.name=='nt'
    expected=flags(stage,profile,windows);cache=(bd/'CMakeCache.txt').read_text(encoding='utf-8-sig')
    for lang in ('C','CXX'):
        m=re.search(r'^CMAKE_'+lang+r'_FLAGS_RELEASE:STRING=(.*)$',cache,re.M);require(m and m.group(1).strip()==expected,'Release flag drift '+lang)
    rows=load(bd/'compile_commands.json');require(rows,'missing compile commands')
    seen=set()
    for r in rows:
        cmd=r['command']
        if scope:
            obj=r.get('output','').replace('\\','/')
            match=re.search(r'(?:^|/)CMakeFiles/([^/]+)\.dir/',obj)
            require(match,'cannot classify compile target '+str(r))
            target=match.group(1);seen.add(target)
            disabled=scope and stage!='full' and target not in PRODUCTION_TARGETS
            overrides=re.findall(r'(?<!\S)(?:/clang:)?-fno-profile-[^\s"]+',cmd)
            require(overrides==([disable_flag(stage,windows)] if disabled else []),'incorrect PGO target scope '+target)
            if disabled:
                require(cmd.index(disable_flag(stage,windows))>cmd.index(extra_flags(stage,profile,windows)[0]),'PGO override order '+target)
        else:
            require(not re.search(r'(?<!\S)(?:/clang:)?-fno-profile-',cmd),'unexpected PGO override')
        require(re.findall(r'(?<!\S)(?:/clang:)?-fprofile-[^\s"]+',cmd)==[f for f in extra_flags(stage,profile,windows) if '-fprofile-' in f],'profile flag drift '+r['file'])
        require(re.findall(lto.OPT,cmd)==re.findall(lto.OPT,expected),'optimization drift')
        require(cmd.count(lto.native_flag(windows))==1 and re.findall(r'(?<!\S)(?:/clang:)?-flto\S*',cmd)==[('/clang:' if windows else '')+'-flto=full'],'native/LTO drift')
        require(all(x in cmd for x in expected.split()),'missing Release flag')
        require(not re.search(r'(?<!\S)(?:/clang:)?(?:-fno-lto|-f(?:no-)?inline\S*|[-/]Ofast|/GL|/LTCG\S*|-fcoverage\S*)(?=\s|$)',cmd,re.I),'unexpected flags')
        require(not re.search(r'(?<!\S)(?:/arch:[^\s]+|(?:/clang:)?-m(?:arch|cpu|tune)=[^\s]+)',cmd.replace(lto.native_flag(windows),'')),'CPU drift')
    if required_targets is not None:require(set(required_targets)<=seen,'missing required production target(s) '+str(set(required_targets)-seen))
    return rows

def probe_target(pair,env,dest,e,name,stage,expected,profile=None):
    source=e/(name+'-probe.cpp');source.write_text('int probe(unsigned long long x){return __builtin_popcountll(x);}\n')
    windows=os.name=='nt';f=flags(stage,profile,windows).split()
    args=[pair[1],*f,*(['/nologo','/clang:-###','/c','/TP'] if windows else ['-###','-c','-x','c++']),str(source)]
    proc=subprocess.run(args,cwd=dest,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',timeout=60)
    (e/(name+'-target-driver.log')).write_text(proc.stdout,encoding='utf-8');require(proc.returncode==0,'driver failed')
    t=lto.parse_target(proc.stdout);require(t['effectiveOpt']==3 and '"-flto=full"' in proc.stdout,'effective O3 or Full LTO missing')
    gen='"-fprofile-instrument=clang"' in proc.stdout;use=re.findall(r'"-fprofile-instrument-use-path=([^"]+)"',proc.stdout)
    require(gen==(stage=='gen') and bool(use)==(stage=='pgo'),'cc1 PGO mode mismatch')
    if use:require(len(use)==1 and P(use[0]).resolve()==P(profile).resolve(),'wrong cc1 profile path')
    if stage!='full':
        # Check the actual driver translation of the later target override.
        excluded_args=[pair[1],*f,disable_flag(stage,windows),*(['/nologo','/clang:-###','/c','/TP'] if windows else ['-###','-c','-x','c++']),str(source)]
        excluded=subprocess.run(excluded_args,cwd=dest,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',timeout=60)
        (e/(name+'-excluded-target-driver.log')).write_text(excluded.stdout,encoding='utf-8')
        require(excluded.returncode==0 and '"-fprofile-instrument=clang"' not in excluded.stdout and '"-fprofile-instrument-use-path=' not in excluded.stdout,'test-only target still uses instrumentation/profile')
        disabled_target=lto.parse_target(excluded.stdout)
        require(disabled_target['effectiveOpt']==3 and '"-flto=full"' in excluded.stdout and all(disabled_target[k]==t[k] for k in ('targetCpu','features','triple','inlineFlags')),'test-only override changed target or optimization')
    t.update(compiler=pair[1],version=load(e/(name+'-version-comparison.json'))['observed'],native=True,releaseFlags=f,lto='full',pgo=stage,profileSHA256=digest(P(profile)) if profile else None)
    require(all(t[k]==expected[k] for k in ('targetCpu','features','triple','version','inlineFlags')),'accepted target drift');write(e/(name+'-target.json'),t)

def check_links(log,stage,tools,profile=None,windows=None):
    if windows is None:windows=os.name=='nt'
    links=lto.check_links(log,'full',tools,windows)
    for line in links:
        if windows and stage=='gen':require(P(tools['profileRuntime']).name.lower() in line.lower(),'Windows profile runtime missing from linker command')
        elif not windows:
            for f in extra_flags(stage,profile,False):require(f in line,'PGO link-stage flag missing')
    return links

def validate_training(data):
    def ident(fen):return tuple(fen.split()[:3])
    cases=data['cases'];require(len(cases)==48 and len({c['id'] for c in cases})==48,'training count')
    keys={ident(c['fen']) for c in cases};require(len(keys)==48,'duplicate training positions')
    require(all(c['depth'] in (4,5) for c in cases),'training depth drift');return cases

def train(build,e,stage,tools,data):
    dest,exe,env=build;out=e/'training'/stage;out.mkdir(parents=True);raw=out/'raw';raw.mkdir();cases=validate_training(data)
    configurations=[('qs-off',False,False,cases),('qs-off-tt-off',False,True,cases[::4])]
    if stage!='main':configurations += [('qs-on',True,False,cases),('qs-on-tt-off',True,True,cases[::4])]
    material=exe.parent/('material_evaluator.dll' if os.name=='nt' else 'libmaterial_evaluator.so')
    pst=exe.parent/('material_pst_evaluator.dll' if os.name=='nt' else 'libmaterial_pst_evaluator.so')
    require(material.is_file() and pst.is_file(),'both packaged native evaluators required for training')
    configurations=[(name+'-C1',on,off,selected,pst) for name,on,off,selected in configurations]+[(name+'-material',on,off,selected,None) for name,on,off,selected in configurations]
    for name,on,tt_off,selected,module in configurations:
        local=dict(env);local.update({k:v for k,v in native_env(on,tt_off=tt_off).items() if k.startswith('CELAB_S')});local['LLVM_PROFILE_FILE']=str(raw/(name+'-%m-%p.profraw'))
        engine=Engine(exe,local,out/(name+'-stderr.log'),module)
        try:
            for c in selected:
                r=engine.search(c['fen'],c['depth']);r.update(id=c['id'],fen=c['fen'],depth=c['depth'],arm=name);append(out/'searches.jsonl',r)
        finally:engine.close()
        require(engine.p.returncode==0,'training engine did not exit normally '+name)
        require(any(p.stat().st_size>0 for p in raw.glob(name+'-*.profraw')),'missing raw profiles '+name)
    paths=sorted(raw.glob('*.profraw'));require(paths and all(p.stat().st_size>0 for p in paths),'empty raw profile')
    merged=out.parent/('training-'+stage+'.profdata');command([tools['profdata'],'merge','-o',merged,*paths],dest,out/'merge.log',env)
    require(merged.stat().st_size>0,'empty merged profile')
    command([tools['profdata'],'show','--all-functions','--counts',merged],dest,out/'profile-show.txt',env)
    show=(out/'profile-show.txt').read_text(errors='replace');require(re.search(r'Maximum function count:\s*[1-9]\d*',show),'zero profile execution counts')
    if stage!='main':
        for fn in ('s17e_recapture','native_read_search_identity') + (('native_fill_evaluation_position',) if stage in ('s113','profile13') else ()):
            require(any(fn in block.splitlines()[0] and re.search(r'Function count: [1-9]\d*',block) for block in re.split(r'(?m)^  (?=\S)',show) if block.strip()),'expected executed hot function absent: '+fn)
    shutil.copyfile(merged,out/'merged.profdata');write(out/'inventory.json',dict(profileSha256=digest(merged),raw=[dict(path=p.name,bytes=p.stat().st_size,sha256=digest(p)) for p in paths],searches=sum(len(c) for _,_,_,c,_ in configurations),trainingSHA256=digest(e/'training-corpus.json')))
    return merged

def preflight(pair,env,root,e,tools):
    """Exercise real instrumentation runtime, profile merging and clean use build before the engine."""
    d=root/'pgo-preflight';d.mkdir();out=e/'pgo-preflight';out.mkdir()
    (d/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.21)
project(pgo_probe C CXX)
add_library(chess_core STATIC core.cpp)
add_library(material_evaluator SHARED evaluator.cpp)
add_executable(chess_lab_uci main.cpp)
target_link_libraries(chess_lab_uci PRIVATE chess_core material_evaluator)
add_executable(untrained_test untrained.cpp core.cpp)
target_compile_definitions(untrained_test PRIVATE TEST_VARIANT=1)
''')
    (d/'core.cpp').write_text('#ifdef TEST_VARIANT\nint core(int n){if(n>0)return n+3;return 0;}\n#else\nint core(int n){return n*7;}\n#endif\n')
    (d/'untrained.cpp').write_text('extern int core(int); int main(int argc,char**){if(argc==1)return core(2)==5?0:1;return 2;}')
    (d/'evaluator.cpp').write_text('#ifdef _WIN32\n#define EX __declspec(dllexport)\n#else\n#define EX\n#endif\nextern "C" EX int evaluator(int x){return x+1;}')
    (d/'main.cpp').write_text('extern int core(int); extern "C" int evaluator(int); int main(){return core(3)+evaluator(4)==26?0:1;}')
    prof=d/'training.profdata';raw=d/'raw';raw.mkdir()
    for stage in ('full','gen','pgo'):
        bd=d/stage;profile=prof if stage=='pgo' else None
        command(['cmake','-S',d,'-B',bd,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DCMAKE_C_COMPILER={pair[0]}',f'-DCMAKE_CXX_COMPILER={pair[1]}','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',*cmake_flags(stage,tools,profile,scope=True)],d,out/(stage+'-configure.log'),env)
        check_regeneration(bd,stage);rows=check_compile(bd,stage,profile,scope=True,required_targets={'chess_core','material_evaluator','chess_lab_uci','untrained_test'});command(['cmake','--build',bd],d,out/(stage+'-build.log'),env);lto.check_objects(bd,rows,'full')
        log=out/(stage+'-links.log');command(['ninja','-C',bd,'-t','commands'],d,log,env);check_links(log,stage,tools,profile)
        command([bd/('chess_lab_uci.exe' if os.name=='nt' else 'chess_lab_uci')],bd,out/(stage+'-execute.log'),dict(env,LLVM_PROFILE_FILE=str(raw/'%m-%p.profraw')))
        if stage!='gen':
            command([bd/('untrained_test.exe' if os.name=='nt' else 'untrained_test')],bd,out/(stage+'-test-execute.log'),env)
        if stage=='gen':
            paths=sorted(raw.glob('*.profraw'));require(paths and all(p.stat().st_size>0 for p in paths),'preflight profile generation failed')
            command([tools['profdata'],'merge','-o',prof,*paths],d,out/'merge.log',env);command([tools['profdata'],'show','--all-functions','--counts',prof],d,out/'show.log',env)
            show=(out/'show.log').read_text(errors='replace');require(all(re.search(r'(?m)^  '+f+r':\s*\n(?:    [^\n]*\n)*?    Function count: [1-9]',show) for f in ('main','evaluator')),'preflight executable/shared evaluator profile missing')
            for p in paths:p.unlink()
        else:require(not list(raw.glob('*.profraw')),'unexpected instrumentation in preflight '+stage)
    write(out/'status.json',{'status':'PASS','executableAndSharedLibrary':True,'profileUseNoRawOutput':True,'untrainedTestTargetScoped':True})
