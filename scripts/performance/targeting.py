"""Compare LLVM's effective O3/O2 while freezing the accepted native CPU target."""
from support import *

def base_compiler(name):
    require(name.endswith(('o2','o3')),'unknown optimization arm '+name)
    return name[:-2]

def native_flag(windows=None):
    if windows is None: windows=os.name=='nt'
    return '/clang:-march=native' if windows else '-march=native'

def release_flags(level,windows=None):
    if windows is None: windows=os.name=='nt'
    require(level in (2,3),'invalid optimization level')
    # clang-cl /O2 actually means LLVM -O3. Preserve every accepted flag and
    # append an explicit Clang override ONLY for the O2 candidate.
    if windows: return '/O2 /Ob2 /DNDEBUG /clang:-march=native'+(' /clang:-O2' if level==2 else '')
    return f'-O{level} -DNDEBUG -march=native'

OPT=r'(?<!\S)(?:/O[^\s]*|/clang:-O[^\s]*|-O[^\s]*)(?=\s|$)'
def check_compile_flags(bd,level,windows=None):
    if windows is None: windows=os.name=='nt'
    flags=release_flags(level,windows)
    cache=(bd/'CMakeCache.txt').read_text(encoding='utf-8-sig')
    for lang in ('C','CXX'):
        found=re.search(r'^CMAKE_'+lang+r'_FLAGS_RELEASE:STRING=(.*)$',cache,re.M)
        require(found and found.group(1).strip()==flags,'unexpected Release flags '+lang)
    rows=load(bd/'compile_commands.json'); require(rows,'empty compile commands')
    for row in rows:
        cmd=row['command']; require(cmd.count(native_flag(windows))==1,'target flag mismatch '+row['file'])
        require(re.findall(OPT,cmd)==re.findall(OPT,flags),'optimization flag sequence mismatch '+row['file'])
        require(not re.search(r'(?<!\S)(?:/arch:[^\s]+|-m(?:arch|cpu|tune)=[^\s]+|/clang:-m(?:arch|cpu|tune)=[^\s]+)',cmd.replace(native_flag(windows),'')),'unexpected additional CPU target flags '+row['file'])
        require(not re.search(r'(?<!\S)(?:/clang:)?(?:-flto\S*|/GL|/LTCG\S*|-fprofile-\S*|-f(?:no-)?inline\S*|[-/]Ofast)(?=\s|$)',cmd,re.I),'unexpected LTO, PGO, fast math or inlining override '+row['file'])
        require(all(flag in cmd for flag in flags.split()),'missing Release flag '+row['file'])
    return len(rows)

def parse_target(output):
    cpu=re.findall(r'"-target-cpu"\s+"([^"]+)"',output)
    require(len(cpu)==1,'expected one driver target CPU')
    features=sorted(set(re.findall(r'"-target-feature"\s+"([+-][^"]+)"',output)))
    triple=re.findall(r'"-triple"\s+"([^"]+)"',output)
    require(len(triple)==1 and triple[0].startswith('x86_64'),'expected x86-64 target')
    opts=re.findall(r'"-O([0-9sz])"',output)
    require(len(opts)==1 and opts[0] in ('2','3'),'expected one effective O2 or O3 in cc1')
    inline=re.findall(r'"(-f(?:no-)?inline[^" ]*)"',output)
    return {'targetCpu':cpu[0],'features':features,'triple':triple[0],'effectiveOpt':int(opts[0]),'inlineFlags':inline}
