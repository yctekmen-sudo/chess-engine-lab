from __future__ import annotations
import argparse, collections, hashlib, json, os, pathlib, queue, re, shutil, statistics, subprocess, sys, tempfile, threading, time, traceback, zipfile
P=pathlib.Path; KIT=P(__file__).resolve().parent

def load(p): return json.loads(p.read_text(encoding='utf-8-sig'))
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,obj): p.write_text(json.dumps(obj,indent=2)+'\n',encoding='utf-8')
def require(ok,msg):
    if not ok: raise RuntimeError(msg)
def command(args, cwd, log, env=None, timeout=1800):
    args=[str(x) for x in args]; print('RUN',log.name,flush=True)
    with log.open('wb') as f:
        f.write((repr(args)+'\n').encode()); f.flush()
        result=subprocess.run(args,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
    require(result.returncode==0,f'{log.name} exit {result.returncode}')
def capture(args,cwd): return subprocess.check_output([str(x) for x in args],cwd=cwd,stderr=subprocess.STDOUT,timeout=120).decode('utf-8',errors='replace').rstrip('\r\n')
def native_env(on=True,tt_off=False):
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('CELAB_S','CELAB_T','CELAB_H'))}
    env['CELAB_S1_QUIESCENCE']=str(int(on))
    # Harness-only key; Engine translates this to UCI Hash=0.
    if tt_off:env['CELAB_S111D_TT_OFF']='1'
    return env

class Engine:
    def __init__(self,exe,env,stderr):
        self.err=stderr.open('wb'); self.p=subprocess.Popen([str(exe)],cwd=exe.parent,env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.err,encoding='utf-8',errors='replace',bufsize=1)
        self.q=queue.Queue(); self.t=threading.Thread(target=self.read,daemon=True);self.t.start()
        try:
            self.send('uci');self.until('uciok');self.send('setoption name Hash value '+('0' if env.get('CELAB_S111D_TT_OFF')=='1' else '16')); self.send('setoption name NumericTT value true');self.send('setoption name MultiPV value 1');self.ready()
        except BaseException:
            self.close();raise
    def read(self):
        for line in self.p.stdout: self.q.put(line.rstrip('\r\n'))
        self.q.put(None)
    def send(self,line): self.p.stdin.write(line+'\n'); self.p.stdin.flush()
    def until(self,prefix,timeout=180):
        lines=[];end=time.monotonic()+timeout
        while True:
            try: line=self.q.get(timeout=max(0.01,end-time.monotonic()))
            except queue.Empty: raise RuntimeError('UCI timeout waiting for '+prefix+'; recent '+repr(lines[-8:]))
            require(line is not None,'UCI process exited; recent '+repr(lines[-8:]))
            lines.append(line)
            if line.startswith(prefix): return lines
    def ready(self): self.send('isready'); self.until('readyok')
    def search(self,fen,depth):
        self.send('ucinewgame');self.send('setoption name Clear Hash');self.send('position fen '+fen);self.ready()
        start=time.perf_counter_ns();self.send('go depth '+str(depth)); lines=self.until('bestmove '); elapsed=time.perf_counter_ns()-start
        return parse(lines,depth,elapsed)
    def close(self):
        try:
            if self.p.poll() is None: self.send('quit');self.p.wait(timeout=8)
        except Exception: self.p.kill();self.p.wait()
        finally:self.err.close()

def parse(lines,depth,elapsed):
    final=[]; metrics=[]; traces=[]; depths=[];raw=None
    for line in lines:
        if line.startswith('info string chesslab score raw white '): raw=line.split()[-1]
        elif line.startswith('info depth '):
            t=line.split(); require(t[t.index('multipv')+1]=='1','unexpected multipv'); pv=t[t.index('pv')+1:] if 'pv' in t else []
            uci_ms=int(t[t.index('time')+1]); score=['raw',raw] if 'score' not in t else t[t.index('score')+1:t.index('score')+3]
            depths.append(dict(depth=int(t[t.index('depth')+1]),nodes=int(t[t.index('nodes')+1]),pv=pv,score=score,raw=raw));raw=None
        elif line.startswith('info string chesslab final_score '): final.append(line.split())
        elif line.startswith('info string chesslab metrics '): metrics.append(line.split()[4:])
        elif line.startswith('CELAB_'): traces.append(line)
    require(len(final)==1 and len(metrics)==1 and depths and depths[-1]['depth']==depth,'incomplete search '+repr(lines[-12:]))
    require([x['depth'] for x in depths]==list(range(1,depth+1)),'missing or repeated completed depth')
    t=final[0]; require(t[t.index('status')+1]=='completed','search incomplete'); require(int(t[t.index('depth')+1])==depth,'final depth mismatch')
    best=lines[-1].split()[1];require(best==t[t.index('bestmove')+1],'final bestmove mismatch')
    kind,value=t[t.index('score')+1:t.index('score')+3]
    return dict(wallNs=elapsed,uciMs=uci_ms,nodes=depths[-1]['nodes'],bestmove=best,scoreKind=kind,scoreValue=value,pv=depths[-1]['pv'],depths=depths,metrics=metrics[0],raw=lines)

def key(r): return {k:r[k] for k in ('nodes','bestmove','scoreKind','scoreValue','pv','depths','metrics')}
def append(file,r):
    with file.open('a',encoding='utf-8') as f:f.write(json.dumps(r,separators=(',',':'))+'\n')
