"""Explicit LTO modes, with a linker-matched control on Linux."""
from support import *
from targeting import release_flags, native_flag, OPT, parse_target

MODES_LTO=('plain','lld','thin','full')
def arms(windows=None):
    if windows is None:windows=os.name=='nt'
    return ['plain','thin','full'] if windows else list(MODES_LTO)
def mode_of(name):
    m=name.split('-',1)[0];require(m in MODES_LTO,'invalid LTO arm '+name);return m
def flags_for(mode,windows=None):
    if windows is None:windows=os.name=='nt'
    return release_flags(3,windows)+((' /clang:' if windows else ' ')+ '-flto='+mode if mode in ('thin','full') else '')
def discover_tools(pair,env):
    path=env['PATH']; windows=os.name=='nt';d=P(pair[1]).parent
    def find(names):
        for n in names:
            p=d/n
            if p.is_file():return str(p)
            p=shutil.which(n,path=path)
            if p:return p
        raise RuntimeError('Missing LLVM tool: '+', '.join(names)+('; install matching lld (Ubuntu: sudo apt install lld-18)' if not windows else '; check Visual Studio Clang installation'))
    version=subprocess.check_output([pair[1],'--version'],env=env,text=True,errors='replace');major=re.search(r'clang version (\d+)',version).group(1)
    linker=find(['lld-link.exe'] if windows else ['ld.lld-'+major,'ld.lld'])
    ar=find(['llvm-lib.exe'] if windows else ['llvm-ar-'+major,'llvm-ar'])
    ranlib=None if windows else find(['llvm-ranlib-'+major,'llvm-ranlib'])
    link_version=subprocess.check_output([linker,'--version'],env=env,text=True,errors='replace').strip()
    require(re.search(r'\bLLD\s+'+major+r'\.',link_version,re.I),'LLD major version mismatch: '+link_version)
    return dict(linker=linker,archiver=ar,ranlib=ranlib,linkerVersion=link_version,compilerVersion=version)
def cmake_flags(mode,tools,windows=None):
    if windows is None:windows=os.name=='nt'
    flags=flags_for(mode,windows)
    args=[f'-DCMAKE_{lang}_FLAGS_RELEASE={flags}' for lang in ('C','CXX')]
    if windows:
        args += [f'-DCMAKE_LINKER={tools["linker"]}',f'-DCMAKE_AR={tools["archiver"]}']
    elif mode!='plain':
        # Explicit driver linker path, and the same LLVM archiver for all LLD modes.
        args += [f'-DCMAKE_{kind}_LINKER_FLAGS_RELEASE=--ld-path="{tools["linker"]}"' for kind in ('EXE','SHARED','MODULE')]
        args += [f'-DCMAKE_AR={tools["archiver"]}',f'-DCMAKE_RANLIB={tools["ranlib"]}']
    return args

def check_compile(bd,mode,windows=None):
    if windows is None:windows=os.name=='nt'
    flags=flags_for(mode,windows);cache=(bd/'CMakeCache.txt').read_text(encoding='utf-8-sig')
    for lang in ('C','CXX'):
        found=re.search(r'^CMAKE_'+lang+r'_FLAGS_RELEASE:STRING=(.*)$',cache,re.M)
        require(found and found.group(1).strip()==flags,'unexpected Release flags '+lang)
    rows=load(bd/'compile_commands.json'); require(rows,'empty compile commands')
    expected_lto=re.findall(r'(?<!\S)(?:/clang:)?-flto\S*',flags)
    for row in rows:
        cmd=row['command'];require(cmd.count(native_flag(windows))==1,'native target drift '+row['file'])
        require(re.findall(OPT,cmd)==re.findall(OPT,flags),'optimization drift '+row['file'])
        require(re.findall(r'(?<!\S)(?:/clang:)?-flto\S*',cmd)==expected_lto,'LTO flag drift '+row['file'])
        require(not re.search(r'(?<!\S)(?:/clang:)?(?:/GL|/LTCG\S*|-fprofile-\S*|-f(?:no-)?inline\S*|[-/]Ofast|-fno-lto)(?=\s|$)',cmd,re.I),'unexpected flags '+row['file'])
        require(not re.search(r'(?<!\S)(?:/arch:[^\s]+|-m(?:arch|cpu|tune)=[^\s]+|/clang:-m(?:arch|cpu|tune)=[^\s]+)',cmd.replace(native_flag(windows),'')),'CPU drift '+row['file'])
        require(all(flag in cmd for flag in flags.split()),'missing flags '+row['file'])
    return rows

def bitcode(path):
    with path.open('rb') as f:return f.read(4) in (b'BC\xc0\xde',b'\xde\xc0\x17\x0b')
def check_objects(bd,rows,mode):
    out=[]
    for row in rows:
        obj=P(row['output']);obj=obj if obj.is_absolute() else P(row['directory'])/obj
        require(obj.is_file(),'object missing '+str(obj))
        bc=bitcode(obj);require(bc==(mode in ('thin','full')),'wrong object format '+str(obj))
        out.append(dict(path=str(obj),bytes=obj.stat().st_size,sha256=digest(obj),bitcode=bc))
    return out

def check_links(log,mode,tools,windows=None):
    if windows is None:windows=os.name=='nt'
    lines=log.read_text(errors='replace').splitlines(); link=[]
    # Ninja emits one command per edge; CMake's native executable and DLL links
    # use /out: or -o respectively, but never the compile-only option -c or /c.
    for line in lines:
        if windows:
            if 'vs_link_exe' not in line and 'vs_link_dll' not in line:continue
            require(P(tools['linker']).name.lower() in line.lower(),'wrong Windows linker');link.append(line)
        else:
            if not re.search(r'\s-o\s',line) or re.search(r'\s-c\s',line) or 'clang' not in line:continue
            require((('--ld-path=' in line and tools['linker'] in line) if mode!='plain' else '--ld-path=' not in line and '-fuse-ld=' not in line),'linker flag drift')
            require(re.findall(r'(?<!\S)-flto\S*',line)==(['-flto='+mode] if mode in ('thin','full') else []),'link-stage LTO mismatch');link.append(line)
    require(link and any('chess_lab_uci' in l for l in link),'missing UCI link command')
    require(any('material_evaluator' in l for l in link),'missing shared evaluator link command')
    return link
