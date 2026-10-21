import { randomUUID } from 'node:crypto';
import { ManagedEngine, type EngineConfig, type SearchLimit } from './engine.js';
import { Game } from './game.js';
import { FideRules, type RecordState } from './rules.js';
export async function analyze(engine:EngineConfig,record:RecordState,limit:SearchLimit) {
  const rules=new FideRules(record),game=new Game(record),process=new ManagedEngine(engine);const progress:unknown[]=[];
  try {
    const identity=await process.start(),session=randomUUID();
    const result=await process.search({session,binding:game.binding(),record,limit,onProgress:p=>{progress.push(p);if(progress.length>128)progress.shift();}});
    const terminal=rules.terminal();
    if(terminal&&result.bestmove!=='0000')throw Error('terminal engine proposed PLAY');
    if(result.bestmove==='0000'&&!terminal)throw Error('nonterminal 0000');
    if(!terminal&&!rules.legal().includes(result.bestmove))throw Error('illegal analysis bestmove');
    if(result.decision.kind!=='play'&&!rules.claim(result.decision.kind==='claim_draw_with_intended_move'?result.decision.move:undefined))throw Error('unverified analysis claim');
    return {identity,result,progress,record:rules.record(),terminal};
  }finally{await process.close();}
}
