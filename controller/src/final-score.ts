import type { FinalSearch } from './game.js';
import { FideRules, type Decision, type RecordState } from './rules.js';

// Only called for an explicitly trusted, SHA-verified Chess Lab process, on its
// single active session. The envelope must directly precede this bestmove.
export function parseFinalSearch(line:string,bestmove:string,maxDepth=64,expectedSearch?:number):FinalSearch {
  const match=/^info string chesslab final_score v1 search ([1-9]\d*) depth ([1-9]\d*) status (completed|cancelled) bestmove ([a-h][1-8][a-h][1-8][qrbn]?) score (finite|white_mate|black_mate) (-?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?)$/.exec(line);
  if(!match)throw Error('malformed final-score envelope');
  const search=Number(match[1]),depth=Number(match[2]),kind=match[5]!,value=Number(match[6]);
  if(!Number.isSafeInteger(search)||(expectedSearch!==undefined&&search!==expectedSearch)||!Number.isSafeInteger(depth)||depth>maxDepth||match[4]!==bestmove||!Number.isFinite(value))throw Error('invalid final-score search/depth/move/value');
  if(kind!=='finite'&&(!/^[1-9]\d*$/.test(match[6]!)||!Number.isSafeInteger(value)||value>depth))throw Error('invalid final mate distance');
  return {contract:'chesslab-final-score-v1',search,depth,status:match[3] as 'completed'|'cancelled',score:kind==='finite'?{kind,value}:{kind:kind as 'white_mate'|'black_mate',distance:value}};
}
export function arbitrateRootClaim(record:RecordState,move:string,final:FinalSearch):Decision {
  const rules=new FideRules(record),score=final.score;
  if(rules.terminal()||!rules.legal().includes(move))return {kind:'play',move};
  const favorable=score.kind==='finite'?(rules.side()==='w'?score.value>0:score.value<0)
    :(score.kind==='white_mate')===(rules.side()==='w');
  if(!favorable){
    if(rules.claim())return {kind:'claim_draw'};
    const intended=rules.legal().filter(m=>rules.claim(m)).sort()[0];
    if(intended)return {kind:'claim_draw_with_intended_move',move:intended};
  }
  return {kind:'play',move};
}
