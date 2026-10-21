import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import { createHash, randomUUID } from 'node:crypto';
import { readFile, realpath, stat } from 'node:fs/promises';
import { isAbsolute } from 'node:path';
import { StringDecoder } from 'node:string_decoder';
import { monotonic, type Binding, type MonotonicClock, type Proposal } from './game.js';
import { DEFAULT_POSITION, type Decision, type RecordState } from './rules.js';
import { reportedStats, type ProtocolLine, type SearchEvidence } from './diagnostics.js';
export type EngineConfig={path:string;args?:string[];kind?:'uci'|'chesslab';sha256?:string;options?:Record<string,string>;handshakeMs?:number;stopGraceMs?:number};
export type EngineState='STARTING'|'UCI_HANDSHAKE'|'READY'|'SEARCHING'|'STOPPING'|'FAILED'|'CLOSED';
export type SearchLimit={kind:'depth';depth:number}|{kind:'movetime';ms:number}|{kind:'clock';wtime:number;btime:number;winc:number;binc:number}|{kind:'infinite'};
export type Option={type:string;default:string;min?:number;max?:number;choices?:string[]};
export type EngineIdentity={path:string;sha256:string;args:string[];id:string[];options:Record<string,Option>;effective:Record<string,string>;m2:string[]|'unknown'};
export type SearchRequest={session:string;binding:Binding;record:RecordState;limit:SearchLimit;stopAfterMs?:number;onProgress?:(p:{session:string;line:string;receivedAt:number;provisional:true})=>void};
type Waiter={line:string;resolve:()=>void;reject:(e:Error)=>void;timer:NodeJS.Timeout};
type Active={request:SearchRequest;resolve:(p:Proposal)=>void;reject:(e:Error)=>void;last:string;claims:number;bytes:number;evidence:SearchEvidence;timer?:NodeJS.Timeout;grace?:NodeJS.Timeout};
export async function executableIdentity(config:EngineConfig):Promise<{path:string;sha256:string}> {
  if(!isAbsolute(config.path)||/[\r\n\0]/.test(config.path))throw Error('executable must be an absolute local path');
  if(config.args?.some(x=>typeof x!=='string'||/[\r\n\0]/.test(x)))throw Error('invalid argv');
  const path=await realpath(config.path);if(!(await stat(path)).isFile())throw Error('executable is not a file');
  const sha256=createHash('sha256').update(await readFile(path)).digest('hex');
  if(config.sha256&&config.sha256!==sha256)throw Error('executable identity mismatch');
  if(config.kind==='chesslab'&&!config.sha256)throw Error('Chess Lab trust requires explicit executable SHA-256');
  return {path,sha256};
}
export interface EngineTransport {
  readonly generation:string;readonly state:EngineState;
  start():Promise<EngineIdentity>;search(request:SearchRequest):Promise<Proposal>;stop():void;close():Promise<void>;
}
export class ManagedEngine implements EngineTransport {
  readonly generation=randomUUID(); state:EngineState='STARTING'; identity:EngineIdentity|undefined; stderr='';
  readonly transcript:string[]=[];
  lastSearchEvidence:SearchEvidence|undefined;
  get protocolComplete():boolean {return this.state==='CLOSED'&&!this.failure&&!this.forcedClose&&this.stdoutEnded&&this.buffer===''&&this.protocolObserverFailures===0;}
  private observerFailures=0;
  get protocolObserverFailures():number {return this.observerFailures;}
  private stdoutEnded=false;private forcedClose=false;
  private protocolSequence=0;private protocolSession:string|undefined;
  private process:ChildProcessWithoutNullStreams|undefined;
  private active:Active|undefined; private waits=new Set<Waiter>(); private failure:Error|undefined;
  private buffer=''; private decoder=new StringDecoder('utf8'); private handshakeBytes=0;
  private observedId:string[]=[]; private observedOptions:Record<string,Option>={}; private m2:string[]=[];
  private stopMs:number; private handshakeMs:number;
  readonly config:EngineConfig;
  constructor(config:EngineConfig,readonly clock:MonotonicClock=monotonic,private readonly onProtocol?:(line:ProtocolLine)=>void) {
    this.config=structuredClone(config);
    if(this.config.args)Object.freeze(this.config.args);
    if(this.config.options)Object.freeze(this.config.options);Object.freeze(this.config);
    this.stopMs=config.stopGraceMs??1000;this.handshakeMs=config.handshakeMs??3000;
    for(const n of [this.stopMs,this.handshakeMs])if(!Number.isSafeInteger(n)||n<1||n>60000)throw Error('invalid protocol timeout');
  }
  private protocol(direction:'in'|'out',line:string,at=this.clock.now()):void {
    this.transcript.push((direction==='out'?'> ':'< ')+line);if(this.transcript.length>128)this.transcript.shift();
    // Diagnostics are observational: an observer must never fail a search.
    try{this.onProtocol?.({sequence:++this.protocolSequence,generation:this.generation,at,direction,line,...(this.protocolSession?{session:this.protocolSession}:{})});}catch{this.observerFailures++;}
  }
  private finishEvidence(a:Active,status:SearchEvidence['status'],at=this.clock.now(),bestmove?:string):void {
    a.evidence.finishedAt=at;a.evidence.measuredElapsedMs=Math.max(0,at-a.evidence.startedAt);a.evidence.status=status;
    if(bestmove!==undefined)a.evidence.bestmove=bestmove;
    this.lastSearchEvidence=structuredClone(a.evidence);
  }
  private send(line:string):void {
    if(this.failure||!this.process||this.state==='CLOSED')throw this.failure??Error('engine not running');
    if(/[\r\n\0]/.test(line)||line.length>65536)throw Error('invalid command line');
    this.protocol('out',line);
    this.process.stdin.write(line+'\n',e=>{if(e)this.fail(e);});
  }
  private fail(e:Error):void {
    if(this.state==='FAILED'||this.state==='CLOSED')return;
    this.failure=Error(`${e.message}; state=${this.state}; generation=${this.generation}; stderr=${this.stderr}`);this.state='FAILED';
    for(const w of this.waits){clearTimeout(w.timer);w.reject(this.failure);}this.waits.clear();
    const a=this.active;this.active=undefined;
    if(a){this.finishEvidence(a,'failed');if(a.timer)clearTimeout(a.timer);if(a.grace)clearTimeout(a.grace);a.reject(this.failure);}
    this.process?.kill('SIGKILL');
  }
  private wait(line:string,command:string):Promise<void> {
    return new Promise((resolve,reject)=>{
      if(this.failure){reject(this.failure);return;}
      const w:Waiter={line,resolve,reject,timer:setTimeout(()=>this.fail(Error('protocol deadline: '+line)),this.handshakeMs)};
      this.waits.add(w);try{this.send(command);}catch(e){this.fail(e as Error);}
    });
  }
  private line(line:string,at:number):void {
    this.protocol('in',line,at);
    if(this.state==='FAILED'||this.state==='CLOSED')return;
    if(this.state==='UCI_HANDSHAKE') {
      this.handshakeBytes+=Buffer.byteLength(line);if(this.handshakeBytes>1024*1024){this.fail(Error('handshake output limit'));return;}
      if(line.startsWith('id '))this.observedId.push(line.slice(3));
      if(line.startsWith('info string chesslab evaluator '))this.m2.push(line);
      if(line.startsWith('option ')) {
        const m=/^option name (.+?) type (check|spin|combo|button|string)(.*)$/.exec(line);
        if(!m){this.fail(Error('malformed option declaration'));return;}
        const name=m[1]!,type=m[2]!,tail=m[3]!;
        if(this.observedOptions[name]){this.fail(Error('duplicate option'));return;}
        const def=/ default (.*?)(?= min | max | var |$)/.exec(tail)?.[1]??'';
        const min=/ min (-?\d+)/.exec(tail)?.[1],max=/ max (-?\d+)/.exec(tail)?.[1];
        this.observedOptions[name]={type,default:def,...(min?{min:Number(min)}:{}),...(max?{max:Number(max)}:{}),...(type==='combo'?{choices:[...tail.matchAll(/ var (.*?)(?= var |$)/g)].map(x=>x[1]!)}:{})};
      }
    }
    if(line==='uciok'||line==='readyok') {
      const w=[...this.waits].find(w=>w.line===line);
      if(w){clearTimeout(w.timer);this.waits.delete(w);w.resolve();}else this.fail(Error('unsolicited '+line));return;
    }
    if(line.startsWith('info string chesslab rejected ')||line.startsWith('info string chesslab fatal ')){this.fail(Error(line));return;}
    const a=this.active;
    if(line.startsWith('bestmove')) {
      if(!a||this.state!=='SEARCHING'&&this.state!=='STOPPING'){this.fail(Error('unsolicited/duplicate bestmove'));return;}
      const m=/^bestmove (0000|[a-h][1-8][a-h][1-8][qrbn]?)(?: ponder [a-h][1-8][a-h][1-8][qrbn]?)?$/.exec(line);
      if(!m){this.fail(Error('malformed bestmove'));return;}
      let decision:Decision={kind:'play',move:m[1]!};
      if(this.config.kind==='chesslab'&&a.claims>0) {
        const c=/^info string chesslab decision (claim_draw|claim_draw_with_intended_move ([a-h][1-8][a-h][1-8][qrbn]?))$/.exec(a.last);
        if(a.claims!==1||!c){this.fail(Error('conflicting, nonadjacent or malformed claim extension'));return;}
        decision=c[1]==='claim_draw'?{kind:'claim_draw'}:{kind:'claim_draw_with_intended_move',move:c[2]!};
      }
      if(a.timer)clearTimeout(a.timer);if(a.grace)clearTimeout(a.grace);
      this.finishEvidence(a,a.evidence.stopRequested?'interrupted':'completed',at,m[1]!);
      this.active=undefined;this.state='STOPPING';
      const proposal:Proposal={session:a.request.session,binding:a.request.binding,receivedAt:at,decision,bestmove:m[1]!};
      this.wait('readyok','isready').then(()=>{
        if(this.failure||this.state==='CLOSED'){if(this.lastSearchEvidence)this.lastSearchEvidence.status='failed';a.reject(this.failure??Error('engine disposed'));return;}this.state='READY';this.protocolSession=undefined;a.resolve(proposal);
      },e=>{if(this.lastSearchEvidence)this.lastSearchEvidence.status='failed';a.reject(e);});return;
    }
    if(a) {
      a.bytes+=Buffer.byteLength(line);if(a.bytes>8*1024*1024){this.fail(Error('search output limit'));return;}
      if(line.startsWith('info string chesslab decision'))a.claims++;a.last=line;
      const reported=reportedStats(line,a.evidence.reported);
      if(reported){
        a.evidence.reported=reported;
        a.evidence.info.push({line:line.slice(0,512),at,truncated:line.length>512});
        if(a.evidence.info.length>4){a.evidence.info.shift();a.evidence.infoDropped++;}
      }
      if(line.startsWith('info '))try{a.request.onProgress?.({session:a.request.session,line,receivedAt:at,provisional:true});}catch(e){this.fail(Error('progress observer: '+String(e)));}
    }
  }
  private data(chunk:Buffer):void {
    const at=this.clock.now();const s=this.decoder.write(chunk);
    for(const part of s.split(/(?<=\n)/)) {
      this.buffer+=part;
      if(Buffer.byteLength(this.buffer)>65536){this.fail(Error('stdout line limit'));return;}
      if(this.buffer.endsWith('\n')){const line=this.buffer.replace(/\r?\n$/,'');this.buffer='';this.line(line,at);}
    }
  }
  async start():Promise<EngineIdentity> {
    if(this.state!=='STARTING')throw Error('engine already started');
    const id=await executableIdentity(this.config);
    if(this.state!=='STARTING')throw this.failure??Error('engine disposed during startup');
    this.state='UCI_HANDSHAKE';
    this.process=spawn(id.path,this.config.args??[],{shell:false,stdio:'pipe'});
    this.process.on('error',e=>this.fail(e));
    this.process.on('exit',(code,signal)=>{if(this.state!=='CLOSED')this.fail(Error(`engine exit ${code}/${signal}`));});
    this.process.stdout.on('data',(b:Buffer)=>this.data(b));
    this.process.stdout.on('end',()=>{this.stdoutEnded=true;});
    this.process.stderr.on('data',(b:Buffer)=>{this.stderr=(this.stderr+b.toString()).slice(-16384);});
    this.process.stdin.on('error',e=>this.fail(e));
    await this.wait('uciok','uci');
    const effective:Record<string,string>={};
    for(const [name,o] of Object.entries(this.observedOptions))if(o.type!=='button')effective[name]=o.default;
    for(const [name,value] of Object.entries(this.config.options??{})) {
      const o=this.observedOptions[name];if(!o)throw await this.abort('unadvertised option '+name);
      if(/[\r\n\0]/.test(name+value))throw await this.abort('invalid option characters');
      if(o.type==='spin'&&(!/^-?\d+$/.test(value)||!Number.isSafeInteger(Number(value))||o.min===undefined||o.max===undefined||Number(value)<o.min||Number(value)>o.max))throw await this.abort('invalid spin option');
      if(o.type==='check'&&value!=='true'&&value!=='false')throw await this.abort('invalid check option');
      if(o.type==='combo'&&!o.choices?.includes(value))throw await this.abort('invalid combo option');
      if(o.type==='button'&&value!=='')throw await this.abort('invalid button option');
      this.send('setoption name '+name+(o.type==='button'?'':' value '+value));effective[name]=value;
    }
    await this.wait('readyok','isready');this.send('ucinewgame');await this.wait('readyok','isready');
    if(this.state!=='UCI_HANDSHAKE')throw this.failure??Error('engine disposed during handshake');
    this.state='READY';
    this.identity={...id,args:[...(this.config.args??[])],id:[...this.observedId],options:structuredClone(this.observedOptions),effective,m2:this.m2.length?[...this.m2]:'unknown'};
    return structuredClone(this.identity);
  }
  private async abort(message:string):Promise<Error>{const e=Error(message);this.fail(e);await this.close();return e;}
  search(request:SearchRequest):Promise<Proposal> {
    if(this.state!=='READY'||this.active)throw Error('search requires READY process');
    request={...request,binding:structuredClone(request.binding),record:structuredClone(request.record),limit:structuredClone(request.limit)};
    let go:string;
    if(request.limit.kind==='depth') {
      if(!Number.isInteger(request.limit.depth)||request.limit.depth<1||request.limit.depth>64)throw Error('invalid depth');go='go depth '+request.limit.depth;
    }else if(request.limit.kind==='movetime') {
      if(!Number.isSafeInteger(request.limit.ms)||request.limit.ms<0||request.limit.ms>2147483647)throw Error('invalid budget');go='go movetime '+request.limit.ms;
    }else if(request.limit.kind==='clock') {
      const {wtime,btime,winc,binc}=request.limit;
      if(![wtime,btime,winc,binc].every(n=>Number.isSafeInteger(n)&&n>=0&&n<=2147483647))throw Error('invalid UCI clock fields');
      go=`go wtime ${wtime} btime ${btime} winc ${winc} binc ${binc}`;
      if(request.stopAfterMs===undefined)throw Error('clock mode requires an authoritative stop budget');
    }else if(request.limit.kind==='infinite')go='go infinite';
    else throw Error('unsupported search mode');
    if(request.stopAfterMs!==undefined&&(!Number.isFinite(request.stopAfterMs)||request.stopAfterMs<0||request.stopAfterMs>2147483647))throw Error('invalid stop budget');
    const position=request.record.complete&&request.record.initialFen===DEFAULT_POSITION?'startpos':'fen '+request.record.initialFen;
    return new Promise((resolve,reject)=>{
      const at=this.clock.now();this.lastSearchEvidence=undefined;this.protocolSession=request.session;
      const a:Active={request,resolve,reject,last:'',claims:0,bytes:0,evidence:{session:request.session,requested:request.limit,startedAt:at,finishedAt:at,measuredElapsedMs:0,status:'failed',stopRequested:false,reported:{},info:[],infoDropped:0}};this.active=a;this.state='SEARCHING';
      try {
        this.send('position '+position+(request.record.moves.length?' moves '+request.record.moves.join(' '):''));this.send(go);
        const limit=request.stopAfterMs??(request.limit.kind==='movetime'?request.limit.ms:undefined);
        if(limit!==undefined)a.timer=setTimeout(()=>this.stop(),limit);
      }catch(e){this.fail(e as Error);}
    });
  }
  stop():void {
    const a=this.active;if(!a||this.state==='STOPPING')return;this.state='STOPPING';
    a.evidence.stopRequested=true;
    try{this.send('stop');a.grace=setTimeout(()=>this.fail(Error('stop grace expired')),this.stopMs);}catch(e){this.fail(e as Error);}
  }
  async close():Promise<void> {
    if(this.state==='CLOSED')return;const child=this.process;
    const closed=Error('engine disposed');this.state='CLOSED';
    for(const w of this.waits){clearTimeout(w.timer);w.reject(closed);}this.waits.clear();
    const a=this.active;this.active=undefined;
    if(a){this.finishEvidence(a,'interrupted');if(a.timer)clearTimeout(a.timer);if(a.grace)clearTimeout(a.grace);a.reject(closed);}
    if(!child||child.exitCode!==null||child.signalCode!==null)return;
    await new Promise<void>(resolve=>{
      const timer=setTimeout(()=>{this.forcedClose=true;child.kill('SIGKILL');resolve();},this.stopMs);timer.unref();
      child.once('close',()=>{clearTimeout(timer);resolve();});
      this.protocol('out','quit');child.stdin.write('quit\n',()=>{});
    });
  }
}
