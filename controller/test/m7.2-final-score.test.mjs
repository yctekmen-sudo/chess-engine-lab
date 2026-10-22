import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createHash,randomUUID} from 'node:crypto';
import {resolve} from 'node:path';
import {ManagedEngine,Game,FideRules,analyze,DEFAULT_POSITION} from '../dist/index.js';
import {parseFinalSearch,arbitrateRootClaim} from '../dist/final-score.js';
import {nativeEngine} from './native-paths.mjs';
const mock=resolve(import.meta.dirname,'mock.mjs');
const hash=createHash('sha256').update(await readFile(process.execPath)).digest('hex');
const record=(side,clock=100)=>({initialFen:`4k3/8/8/8/8/8/8/R3K3 ${side} - - ${clock} 1`,moves:[],complete:false});
const final=score=>({contract:'chesslab-final-score-v1',search:1,depth:1,status:'completed',score});
async function proposal(t,mode,r,trust=true,extra={}){
 const e=new ManagedEngine({path:process.execPath,args:[mock,mode],...(trust?{kind:'chesslab',sha256:hash}:{})});t.after(()=>e.close());await e.start();
 const game=new Game(r),session=randomUUID();const pending=e.search({session,binding:game.begin(session),record:r,limit:{kind:'depth',depth:1},...extra});return {game,e,pending};
}
test('verified finite final scores arbitrate immediate/intended claims, ties and favorable continuations for both colors',async t=>{
 for(const side of ['w','b'])for(const clock of [99,100])for(const mode of ['final-negative','final-positive','final-zero','final-cancelled']){
  const r=record(side,clock),{game,pending}=await proposal(t,mode,r,true,mode==='final-cancelled'?{limit:{kind:'movetime',ms:60000}}:{});const p=await pending;
  const favorable=side==='w'?mode==='final-positive':mode==='final-negative';
  assert.equal(p.decision.kind,favorable?'play':clock===100?'claim_draw':'claim_draw_with_intended_move');
  if(!favorable&&clock===99)assert.equal(p.decision.move,new FideRules(r).legal().filter(m=>new FideRules(r).claim(m)).sort()[0]);
  assert.equal(p.finalSearch.contract,'chesslab-final-score-v1');assert.equal(p.finalSearch.status,mode==='final-cancelled'?'cancelled':'completed');
  game.accept(p);assert.equal(game.record().moves.length,favorable?1:0);
 }
});
test('structured mate scores preserve polarity and immediate claims precede intended claims',()=>{
 for(const side of ['w','b'])for(const kind of ['white_mate','black_mate']){
  const r=record(side),move=new FideRules(r).legal()[0],p=arbitrateRootClaim(r,move,final({kind,distance:1}));
  assert.equal(p.kind,(kind==='white_mate')===(side==='w')?'play':'claim_draw');
 }
 const r={initialFen:DEFAULT_POSITION,moves:['g1f3','g8f6','f3g1','f6g8','g1f3','g8f6','f3g1'],complete:true};
 assert.equal(arbitrateRootClaim(r,new FideRules(r).legal()[0],final({kind:'finite',value:0})).kind,'claim_draw_with_intended_move');
 r.moves.push('f6g8');assert.equal(arbitrateRootClaim(r,new FideRules(r).legal()[0],final({kind:'finite',value:0})).kind,'claim_draw');
});
test('generic/missing-score engines do not gain score-based claims; analysis suppresses automatic arbitration',async t=>{
 for(const [mode,trust,extra] of [['final-zero',false,{}],['final-missing',true,{}],['final-zero',true,{claimPolicy:'none'}]]){
  const {pending}=await proposal(t,mode,record('w'),trust,extra);const p=await pending;assert.equal(p.decision.kind,'play');
  if(!trust||mode==='final-missing')assert.equal(p.finalSearch,undefined);
 }
});
test('malformed, conflicting, nonadjacent and mismatched trusted scores fail the session',async t=>{
 for(const mode of ['final-malformed','final-duplicate','final-nonadjacent','final-mismatch','final-stale']){
  const {pending}=await proposal(t,mode,record('w'));await assert.rejects(pending,/final-score/);
 }
 const {pending}=await proposal(t,'final-zero',record('w'),true,{limit:{kind:'depth',depth:2}});await assert.rejects(pending,/incomplete completed-depth/);
});
test('final parser rejects nonfinite, invalid depth/mate distance and unsupported envelopes',()=>{
 const line='info string chesslab final_score v1 search 1 depth 1 status completed bestmove a1a2 score finite 0';
 for(const bad of [line.replace('finite 0','finite 1e309'),line.replace('depth 1','depth 65'),line.replace('v1','v2'),line.replace('finite 0','white_mate 0'),line.replace('finite 0','black_mate 2')])assert.throws(()=>parseFinalSearch(bad,'a1a2'));
 assert.deepEqual(parseFinalSearch(line,'a1a2').score,{kind:'finite',value:0});
 assert.throws(()=>parseFinalSearch(line,'a1a2',64,2),/search/);
});
test('final-score proposals retain game binding, independent validation and receipt deadlines',async t=>{
 const {game,pending}=await proposal(t,'final-zero',record('w'));const p=await pending;
 assert.throws(()=>game.accept({...p,binding:{...p.binding,generation:99}}),/stale/);
 assert.throws(()=>new Game(record('w')).accept(p),/stale/);
 const initial={initialFen:DEFAULT_POSITION,moves:[],complete:true},{game:fresh,pending:pendingFresh}=await proposal(t,'final-zero',initial);
 const play=await pendingFresh;assert.equal(play.decision.kind,'play');assert.throws(()=>fresh.accept({...play,decision:{kind:'claim_draw'}}),/unverified/);
 let now=0;const timed=new Game(record('w'),{kind:'fischer',initialMs:10,incrementMs:5},{now:()=>now}),session=randomUUID();const binding=timed.begin(session);now=10;
 timed.accept({...p,session,binding,receivedAt:10});assert.equal(timed.record().moves.length,0);assert.equal(timed.events.at(-1).kind,'timeout');
});
test('native trusted final scores select actual-game claims while analysis returns a playable line',async t=>{
 const sha256=createHash('sha256').update(await readFile(nativeEngine)).digest('hex'),config={path:nativeEngine,kind:'chesslab',sha256};
 const r=record('b'),e=new ManagedEngine(config);t.after(()=>e.close());await e.start();const game=new Game(r),session=randomUUID();
 const p=await e.search({session,binding:game.begin(session),record:r,limit:{kind:'depth',depth:1}});assert.equal(p.decision.kind,'claim_draw');assert(p.finalSearch);game.accept(p);assert.equal(game.record().moves.length,0);
 const a=await analyze(config,r,{kind:'depth',depth:1});assert.equal(a.result.decision.kind,'play');assert(a.result.finalSearch);assert.equal(a.record.moves.length,0);
});

async function stoppedProposal(t,mode,r,limit={kind:'depth',depth:3}){
 const e=new ManagedEngine({path:process.execPath,args:[mock,mode],kind:'chesslab',sha256:hash});t.after(()=>e.close());await e.start();
 const game=new Game(r),session=randomUUID();let requestedStop=false;
 const result=await e.search({session,binding:game.begin(session),record:r,limit,onProgress:p=>{
  if(p.line==='info string fixture waiting for stop'){requestedStop=true;e.stop();}
 }});
 assert(requestedStop);assert(e.transcript.includes('> stop'));assert(e.lastSearchEvidence.stopRequested);
 return {e,game,result};
}

test('fixed-depth completed results must reach the requested depth before authorizing a claim',async t=>{
 const {game,e,pending}=await proposal(t,'final-requested-depth',record('w'),true,{limit:{kind:'depth',depth:3}});
 const p=await pending;assert.equal(p.finalSearch.depth,3);assert.equal(p.finalSearch.status,'completed');assert(!e.lastSearchEvidence.stopRequested);
 assert.equal(p.decision.kind,'claim_draw');game.accept(p);assert.equal(game.outcome().reason,'claim_draw');
 const shallow=await proposal(t,'final-zero',record('w'),true,{limit:{kind:'depth',depth:3}});
 await assert.rejects(shallow.pending,/incomplete completed-depth/);assert.equal(shallow.e.state,'FAILED');
 assert(!shallow.e.lastSearchEvidence.stopRequested);assert.equal(shallow.game.outcome(),undefined);assert.equal(shallow.game.record().moves.length,0);
});

test('unexpected fixed-depth cancellation fails before claim arbitration, even at the requested depth or in analysis',async t=>{
 for(const depth of [1,3])for(const claimPolicy of ['controller','none']){
  const {game,e,pending}=await proposal(t,'final-cancelled',record('w'),true,{limit:{kind:'depth',depth},claimPolicy});
  await assert.rejects(pending,/unexpected fixed-depth cancellation without controller stop/);
  assert.equal(e.state,'FAILED');assert.equal(e.lastSearchEvidence.status,'failed');assert(!e.lastSearchEvidence.stopRequested);
  assert(!e.transcript.includes('> stop'));assert.equal(game.outcome(),undefined);assert.equal(game.record().moves.length,0);
 }
});

test('requested fixed-depth stops retain a valid completed-depth score and only the current session stop authorizes cancellation',async t=>{
 for(const side of ['w','b']){
  const {e,game,result}=await stoppedProposal(t,'final-stop-cancelled',record(side));
  assert.equal(result.finalSearch.depth,1);assert.equal(result.finalSearch.status,'cancelled');assert.equal(result.decision.kind,'claim_draw');
  assert.equal(e.lastSearchEvidence.status,'interrupted');game.accept(result);assert.equal(game.record().moves.length,0);assert.equal(game.outcome().reason,'claim_draw');
 }
 // A stop sent while READY belongs to no search and must not authorize the next
 // search's unsolicited cancellation. No timer/receipt race is involved.
 const e=new ManagedEngine({path:process.execPath,args:[mock,'final-cancelled'],kind:'chesslab',sha256:hash});t.after(()=>e.close());await e.start();e.stop();
 const r=record('w'),game=new Game(r),session=randomUUID();
 await assert.rejects(e.search({session,binding:game.begin(session),record:r,limit:{kind:'depth',depth:3}}),/unexpected fixed-depth cancellation/);
 assert(!e.lastSearchEvidence.stopRequested);assert.equal(game.outcome(),undefined);
 const previous=await stoppedProposal(t,'final-stop-once',record('w'));
 const nextGame=new Game(record('w')),nextSession=randomUUID();
 await assert.rejects(previous.e.search({session:nextSession,binding:nextGame.begin(nextSession),record:nextGame.record(),limit:{kind:'depth',depth:3}}),/unexpected fixed-depth cancellation/);
 assert(!previous.e.lastSearchEvidence.stopRequested);assert.equal(previous.e.lastSearchEvidence.status,'failed');assert.equal(nextGame.outcome(),undefined);
});

test('stopped-before-depth-one fallback stays unevaluated and cannot trigger a score-based draw claim',async t=>{
 const {e,game,result}=await stoppedProposal(t,'final-stop-fallback',record('w'));
 assert(new FideRules(game.record()).claim());assert.equal(result.decision.kind,'play');assert.equal(result.finalSearch,undefined);
 assert(e.transcript.includes('< info string chesslab fallback unevaluated'));assert(!e.transcript.some(line=>line.startsWith('< info string chesslab final_score ')));
 assert.deepEqual(e.lastSearchEvidence.reported,{});game.accept(result);assert.equal(game.record().moves.length,1);assert.equal(game.outcome(),undefined);
});

test('movetime, clock and infinite searches still accept engine-local cancellation or completion without controller stop',async t=>{
 const limits=[{kind:'movetime',ms:60000},{kind:'clock',wtime:60000,btime:60000,winc:0,binc:0},{kind:'infinite'}];
 for(const limit of limits)for(const mode of ['final-zero','final-cancelled']){
  const {game,e,pending}=await proposal(t,mode,record('w'),true,{limit,stopAfterMs:60000});const p=await pending;
  assert(!e.lastSearchEvidence.stopRequested);assert(!e.transcript.includes('> stop'));assert.equal(p.finalSearch.depth,1);
  assert.equal(p.finalSearch.status,mode==='final-cancelled'?'cancelled':'completed');assert.equal(p.decision.kind,'claim_draw');
  game.accept(p);assert.equal(game.outcome().reason,'claim_draw');
 }
});

test('generic UCI engines retain ordinary PLAY behavior for unsolicited cancelled fixed-depth score strings',async t=>{
 const {game,e,pending}=await proposal(t,'final-cancelled',record('w'),false,{limit:{kind:'depth',depth:3}});const p=await pending;
 assert.equal(p.decision.kind,'play');assert.equal(p.finalSearch,undefined);assert(!e.lastSearchEvidence.stopRequested);
 game.accept(p);assert.equal(game.record().moves.length,1);
});
