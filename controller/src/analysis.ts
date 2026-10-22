import { randomUUID } from 'node:crypto';
import { ManagedEngine, type EngineConfig, type SearchLimit, type SearchRequest } from './engine.js';
import { Game } from './game.js';
import { FideRules, type RecordState } from './rules.js';
export type AnalysisOptions={searchCapMs?:number;signal?:AbortSignal;onProgress?:SearchRequest['onProgress']};
export async function analyze(engine:EngineConfig,record:RecordState,limit:SearchLimit,options:AnalysisOptions={}) {
  const cap=options.searchCapMs??60000;
  if(!Number.isSafeInteger(cap)||cap<1||cap>2147483647)throw Error('invalid analysis watchdog');
  if(limit.kind==='infinite'&&!options.signal)throw Error('infinite analysis requires an AbortSignal');
  const cancelled=()=>new DOMException('Analysis cancelled','AbortError');
  if(options.signal?.aborted)throw cancelled();
  const rules=new FideRules(record),game=new Game(record),process=new ManagedEngine(engine);const progress:unknown[]=[];
  let disposal:Promise<void>|undefined;
  const abort=()=>{
    if(process.state==='SEARCHING'||process.state==='STOPPING')process.stop();
    else disposal=process.close();
  };
  options.signal?.addEventListener('abort',abort,{once:true});
  try {
    const identity=await process.start(),session=randomUUID();
    if(options.signal?.aborted)throw cancelled();
    const stopAfterMs=limit.kind==='infinite'?undefined:Math.min(cap,limit.kind==='movetime'?limit.ms:cap);
    const result=await process.search({session,binding:game.binding(),record,limit,claimPolicy:'none',...(stopAfterMs!==undefined?{stopAfterMs}:{}),onProgress:p=>{
      progress.push(p);if(progress.length>128)progress.shift();options.onProgress?.(p);
    }});
    if(options.signal?.aborted)throw cancelled();
    const terminal=rules.terminal();
    if(terminal&&result.bestmove!=='0000')throw Error('terminal engine proposed PLAY');
    if(result.bestmove==='0000'&&!terminal)throw Error('nonterminal 0000');
    if(!terminal&&!rules.legal().includes(result.bestmove))throw Error('illegal analysis bestmove');
    if(result.decision.kind!=='play'&&!rules.claim(result.decision.kind==='claim_draw_with_intended_move'?result.decision.move:undefined))throw Error('unverified analysis claim');
    return {identity,result,progress,record:rules.record(),terminal};
  }catch(e){if(options.signal?.aborted)throw cancelled();throw e;}
  finally{options.signal?.removeEventListener('abort',abort);await disposal;await process.close();}
}
