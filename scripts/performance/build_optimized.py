#!/usr/bin/env python3
"""Reproducible native Clang O3 + Full LTO + frontend PGO build, opt-in only."""
import sys
sys.dont_write_bytecode=True
from support import *
import pgo, lto
from targeting import parse_target


def source_inventory(root):
    raw=subprocess.check_output(['git','ls-files','-c','-o','--exclude-standard','-z'],cwd=root)
    names=sorted(set(os.fsdecode(x) for x in raw.split(b'\0') if x))
    return {n:digest(root/n) for n in names if (root/n).is_file()}


def environment(pair):
    env=native_env()
    for k in list(env):
        if k.upper() in {'CC','CXX','CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','CL','_CL_','LINK','_LINK_','CMAKE_TOOLCHAIN_FILE','CMAKE_GENERATOR','CMAKE_BUILD_TYPE','CMAKE_PREFIX_PATH','CMAKE_PROJECT_INCLUDE','CMAKE_PROJECT_INCLUDE_BEFORE','LLVM_PROFILE_FILE'} or k.upper().startswith(('CELAB_S','CELAB_T','CELAB_H')):env.pop(k,None)
    env['PATH']=str(P(pair[1]).parent)+os.pathsep+env.get('PATH','');return env


def build_stage(root,out,pair,tools,env,stage,parallel,profile=None):
    bd=out/('generate' if stage=='gen' else 'use');name=stage
    args=['cmake','--preset','native-release','-B',bd,f'-DCMAKE_C_COMPILER={pair[0]}',f'-DCMAKE_CXX_COMPILER={pair[1]}','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCHESS_LAB_QUIESCENCE_DEFAULT=ON','-DCHESS_LAB_S1_DIAGNOSTICS=OFF',*pgo.cmake_flags(stage,tools,profile,scope=True)]
    command(args,root,out/(name+'-configure.log'),env)
    pgo.check_regeneration(bd,stage)
    rows=pgo.check_compile(bd,stage,profile,scope=True,required_targets=pgo.PRODUCTION_TARGETS)
    # Check actual driver selection, not merely CMake flag strings.
    versions={}
    for lang in ('C','CXX'):
        files=list(bd.glob(f'CMakeFiles/*/CMake{lang}Compiler.cmake'));require(len(files)==1,'compiler metadata missing')
        meta=files[0].read_text(errors='replace');require(f'set(CMAKE_{lang}_COMPILER_ID "Clang")' in meta,'Clang required')
        versions[lang]=re.search(r'set\(CMAKE_'+lang+r'_COMPILER_VERSION "([^"]+)"\)',meta).group(1)
    probe=out/(name+'-probe.cpp');probe.write_text('int probe(unsigned long long x){return __builtin_popcountll(x);}\n')
    windows=os.name=='nt';flags=pgo.flags(stage,profile,windows).split()
    args=[pair[1],*flags,*(['/nologo','/clang:-###','/c','/TP'] if windows else ['-###','-c','-x','c++']),str(probe)]
    proc=subprocess.run(args,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',timeout=60)
    (out/(name+'-driver.log')).write_text(proc.stdout,encoding='utf-8');require(proc.returncode==0,'compiler driver probe failed')
    t=parse_target(proc.stdout);require(t['effectiveOpt']==3 and '"-flto=full"' in proc.stdout,'effective O3/Full LTO missing')
    gen='"-fprofile-instrument=clang"' in proc.stdout;use=re.findall(r'"-fprofile-instrument-use-path=([^"]+)"',proc.stdout)
    require(gen==(stage=='gen') and bool(use)==(stage=='pgo'),'driver PGO mode mismatch')
    if use:require(len(use)==1 and P(use[0]).resolve()==P(profile).resolve(),'profile path mismatch')
    t.update(version=versions,lto='full',pgo=stage,profileSHA256=digest(P(profile)) if profile else None);write(out/(name+'-target.json'),t)
    start=time.perf_counter_ns();command(['cmake','--build',bd,'--parallel',str(parallel)],root,out/(name+'-build.log'),env,timeout=3600)
    (out/(name+'-build-wall-ns.txt')).write_text(str(time.perf_counter_ns()-start)+'\n')
    command(['ninja','-C',bd,'-t','commands'],root,out/(name+'-ninja-commands.log'),env)
    write(out/(name+'-links.json'),pgo.check_links(out/(name+'-ninja-commands.log'),stage,tools,profile))
    write(out/(name+'-objects.json'),lto.check_objects(bd,rows,'full'))
    shutil.copyfile(bd/'compile_commands.json',out/(name+'-compile_commands.json'));shutil.copyfile(bd/'CMakeCache.txt',out/(name+'-CMakeCache.txt'))
    if stage=='pgo':require(not re.search(r'warning:.*(?:profile.*(?:out of date|mismatch|invalid|malformed))',(out/(name+'-build.log')).read_text(errors='replace'),re.I),'stale profile warning')
    exe=bd/('chess_lab_uci.exe' if windows else 'chess_lab_uci');require(exe.is_file(),'engine executable missing')
    binaries=[x for x in bd.rglob('*') if x.is_file() and (x==exe or (x.suffix.lower() in ('.dll','.so','.dylib') and 'CMakeFiles' not in x.parts))]
    require(any('material' in x.name for x in binaries),'material evaluator missing')
    write(out/(name+'-binaries.json'),[dict(path=x.relative_to(bd).as_posix(),sha256=digest(x),bytes=x.stat().st_size) for x in binaries])
    return root,exe,env


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',required=True,type=P);parser.add_argument('--compiler',required=True,type=P);parser.add_argument('--cc',type=P);parser.add_argument('--parallel',type=int,default=4);args=parser.parse_args()
    root=KIT.parent.parent.resolve();out=args.output.resolve();require(not out.exists(),'output must be a fresh directory');require(not re.search(r'\s',out.as_posix()),'output path must not contain whitespace');require(args.parallel>0,'parallel must be positive')
    # Do not allow an output directory that would alter tracked or untracked source inventory.
    if out.is_relative_to(root):require(out.is_relative_to(root/'build'),'in-repo output must be under build/')
    cxx=args.compiler.absolute();require(cxx.is_file(),'compiler not found');cc=args.cc.absolute() if args.cc else cxx if os.name=='nt' else cxx.with_name(cxx.name.replace('clang++','clang'))
    require(cc.is_file(),'matching C compiler missing; specify --cc');pair=(str(cc),str(cxx));env=environment(pair);before=source_inventory(root);out.mkdir(parents=True)
    write(out/'source-before.json',before);head=capture(['git','rev-parse','HEAD'],root);status=capture(['git','status','--porcelain','--untracked-files=all'],root)
    try:
        tools=pgo.discover_pgo(pair,env,root,out);scratch=out/'preflight-work';scratch.mkdir();pgo.preflight(pair,env,scratch,out,tools)
        data=load(KIT/'accepted-training.json');pgo.validate_training(data);write(out/'training-corpus.json',data)
        gen=build_stage(root,out,pair,tools,env,'gen',args.parallel)
        merged=pgo.train(gen,out,'s113',tools,data)
        use=build_stage(root,out,pair,tools,env,'pgo',args.parallel,merged)
        a,b=load(out/'gen-target.json'),load(out/'pgo-target.json');require(all(a[k]==b[k] for k in ('targetCpu','features','triple','version','inlineFlags','effectiveOpt')),'generation/use target drift')
        require(source_inventory(root)==before and capture(['git','status','--porcelain','--untracked-files=all'],root)==status,'source changed during build')
        write(out/'source-after.json',before);write(out/'result.json',dict(status='PASS',head=head,sourceStatus=status,executable=str(use[1]),sha256=digest(use[1]),profileSHA256=digest(merged)))
        print('Optimized executable:',use[1],flush=True)
    except BaseException as exc:
        write(out/'result.json',dict(status='FAIL',head=head,error=str(exc),traceback=traceback.format_exc()));raise
if __name__=='__main__':main()
