import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {resolve} from 'node:path';
import {createHash,randomUUID} from 'node:crypto';
import {readFile} from 'node:fs/promises';
import {FideRules,DEFAULT_POSITION,matePossibility,Game,ManagedEngine,analyze} from '../dist/index.js';
const initial={initialFen:DEFAULT_POSITION,moves:[],complete:true};
const mock=resolve(import.meta.dirname,'mock.mjs'),build=process.env.CHESS_LAB_BUILD??resolve(import.meta.dirname,'../../build/native-debug');
const engine=(mode='normal',extra={})=>({path:process.execPath,args:[mock,mode],handshakeMs:500,stopGraceMs:50,...extra});
const native=resolve(build,'chess_lab_uci');
const record=(initialFen,moves=[],complete=false)=>({initialFen,moves,complete});
const request=(game,limit={kind:'depth',depth:1})=>{const session=randomUUID();return {session,binding:game.begin(session),record:game.record(),limit};};
async function instance(t,mode='normal',extra={}){const e=new ManagedEngine(engine(mode,extra));t.after(()=>e.close());await e.start();return e;}
test('independent Rules vs real ChessCore: generated replay and FIDE edge fixtures',()=>{
 const fixtures=[initial,
 record('4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1'),
 record('k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1'),
 record('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1'),
 record('4k3/P7/8/8/8/8/7p/4K3 w - - 0 1'),
 record('7k/6Q1/5K2/8/8/8/8/8 b - - 150 1'),
 record('7k/5K2/6Q1/8/8/8/8/8 b - - 150 1'),
 record('7k/8/8/8/8/8/q7/4K3 w - - 99 1'),
 record('7k/8/8/8/8/8/q7/4K3 w - - 100 1'),
 record('7k/8/8/8/8/8/q7/4K3 w - - 150 1'),
 record('4k3/8/8/8/8/8/8/4K3 w - - 0 1'),
 record('4k3/8/8/8/8/8/8/2B1K3 w - - 0 1'),
 record('4k3/8/8/8/8/8/8/1N2K3 w - - 0 1'),
 record('4kb2/8/8/8/8/8/8/2B1K3 w - - 0 1'),
 record('4k3/8/8/8/8/8/8/2B1KB2 w - - 0 1')];
 const cycle=['g1f3','g8f6','f3g1','f6g8'];
 for(const count of [1,2,4])for(const complete of [true,false])fixtures.push(record(DEFAULT_POSITION,Array(count).fill(cycle).flat(),complete));
 let r=new FideRules();let seed=7;
 for(let i=0;i<24;i++){if(r.terminal())break;seed=(Math.imul(seed,1664525)+1013904223)>>>0;const legal=r.legal();r.play(legal[seed%legal.length]);fixtures.push(r.record());}
 const output=execFileSync(resolve(build,'m6_rules_probe'),[],{input:fixtures.map(f=>JSON.stringify(f)).join('\n')+'\n',encoding:'utf8',maxBuffer:4*1024*1024,timeout:10000}).trim().split('\n').map(JSON.parse);
 assert.equal(output.length,fixtures.length);
 for(let i=0;i<fixtures.length;i++){
  const r=new FideRules(fixtures[i]),c=output[i];assert(!c.error,JSON.stringify(c));
  assert.deepEqual({fen:r.fen(),signature:r.signature(),legal:r.legal(),occurrences:r.occurrences(),dead:r.dead(),terminal:r.terminal()?.reason??'ongoing',claim:r.claim(),intended:r.legal().filter(m=>r.claim(m))},c,JSON.stringify(fixtures[i]));
 }
});
test('claim intentions remain unplayed; incomplete history retains positive evidence',()=>{
 const r=new FideRules(record('7k/8/8/8/8/8/q7/4K3 w - - 99 1'));
 const before=r.record();assert(r.claim('e1d1'));assert.deepEqual(r.record(),before);
 const r2=new FideRules(record(DEFAULT_POSITION,['g1f3','g8f6','f3g1','f6g8','g1f3','g8f6','f3g1','f6g8'],false));
 assert(r2.claim());assert.equal(r2.complete,false);assert.equal(r2.occurrences(),3);
});
test('timeout proof classes: witnesses, bare king, minor, KNN and cooperating material',()=>{
 for(const fen of ['4k3/8/8/8/8/8/8/4K3 w - - 0 1','4k3/8/8/8/8/8/8/2B1K3 w - - 0 1','4k3/8/8/8/8/8/8/1N2K3 w - - 0 1'])assert.equal(matePossibility(new FideRules(record(fen)),'w').status,'CANNOT_MATE');
 for(const fen of ['4k3/8/8/8/8/8/8/1N2K1N1 w - - 0 1','2b1k3/8/8/8/8/8/8/2B1K3 w - - 0 1','4k3/P7/8/8/8/8/8/4K3 w - - 0 1'])assert.equal(matePossibility(new FideRules(record(fen)),'w',0).status,'UNDETERMINED');
 const r=new FideRules(record('7k/8/5KQ1/8/8/8/8/8 w - - 0 1'));
 const proof=matePossibility(r,'w',1);assert.equal(proof.status,'CAN_MATE');
 for(const m of proof.witness)r.play(m);assert.equal(r.terminal().reason,'checkmate');assert.equal(r.side(),'b');
});
function virtual(){let now=0;return {clock:{now:()=>now},set:n=>{now=n;}};}
function proposal(g,move,at,decision={kind:'play',move}){const session=randomUUID();return {session,binding:g.begin(session),receivedAt:at,bestmove:move,decision};}
test('receipt linearization, exact deadline, increment, stale and illegal candidates',()=>{
 const v=virtual(),g=new Game(initial,{kind:'fischer',initialMs:100,incrementMs:10},v.clock);
 const p=proposal(g,'e2e4',99);v.set(120);g.accept(p);assert.equal(g.clocks().w,11);assert.equal(g.deadline(),220);
 assert.throws(()=>g.accept(p),/stale/);assert.equal(g.record().moves.length,1);
 const v2=virtual(),late=new Game(record('7k/8/8/8/8/8/q7/4K3 w - - 0 1'),{kind:'fischer',initialMs:100,incrementMs:10},v2.clock);
 const p2=proposal(late,'e1d1',100);v2.set(100);late.accept(p2);assert.equal(late.record().moves.length,0);assert.equal(late.clocks().w,0);
 const v3=virtual(),bad=new Game(initial,{kind:'fischer',initialMs:100,incrementMs:10},v3.clock);
 const p3=proposal(bad,'e2e5',20);v3.set(70);assert.throws(()=>bad.accept(p3),/illegal/);assert.equal(bad.deadline(),100);assert.equal(bad.available(),30);
});
test('accepted claims end clocks without increment or playing intended move',()=>{
 const v=virtual(),g=new Game(record('7k/8/8/8/8/8/q7/4K3 w - - 99 1'),{kind:'fischer',initialMs:100,incrementMs:10},v.clock);
 const p=proposal(g,'e1d1',30,{kind:'claim_draw_with_intended_move',move:'e1d1'});v.set(40);g.accept(p);
 assert.equal(g.clocks().w,70);assert.equal(g.record().moves.length,0);assert.equal(g.outcome().result,'1/2-1/2');
});
test('unknown flag is adjudication required; no heuristic chess winner',()=>{
 const v=virtual(),g=new Game(initial,{kind:'fischer',initialMs:1,incrementMs:0},v.clock);v.set(1);
 assert.equal(g.flag().kind,'adjudication_required');assert.equal(g.record().moves.length,0);
});
test('partial stdout/CRLF, serial search barrier and ordinary UCI spoof ignored',async t=>{
 const partial=await instance(t,'partial');const game=new Game(initial);const req=request(game);
 const p=partial.search(req);assert.throws(()=>partial.search(req),/READY/);const result=await p;game.accept(result);assert.equal(game.record().moves.length,1);
 const spoof=await instance(t,'spoof');const r=await spoof.search(request(new Game(initial)));assert.equal(r.decision.kind,'play');
});
for(const mode of ['duplicate','bad','huge','crash'])test('adversarial '+mode+' invalidates session and process',async t=>{
 const e=await instance(t,mode);const g=new Game(initial);
 await assert.rejects(e.search(request(g)));assert.equal(g.record().moves.length,0);assert.equal(e.state,'FAILED');
});
for(const mode of ['hang','stop-late'])test('unresolved stop discards process: '+mode,async t=>{
 const e=await instance(t,mode);const g=new Game(initial);const p=e.search({...request(g),stopAfterMs:1});
 await assert.rejects(p,/stop grace/);assert.equal(e.state,'FAILED');assert.throws(()=>e.search(request(new Game(initial))),/READY/);
});
for(const mode of ['no-uci','no-ready'])test('bounded handshake '+mode,async t=>{
 const e=new ManagedEngine(engine(mode));t.after(()=>e.close());await assert.rejects(e.start(),/deadline/);
});
test('trusted extension provenance requires explicit hash, rejects conflicts/nonadjacent',async t=>{
 await assert.rejects(new ManagedEngine(engine('spoof',{kind:'chesslab'})).start(),/SHA-256/);
 const hash=createHash('sha256').update(await readFile(process.execPath)).digest('hex');
 for(const mode of ['conflict','nonadjacent']) {
  const e=await instance(t,mode,{kind:'chesslab',sha256:hash});await assert.rejects(e.search(request(new Game(initial))),/claim extension/);
 }
});
test('native analysis never plays; trusted claims validated independently',async()=>{
 const hash=createHash('sha256').update(await readFile(native)).digest('hex');
 const r=record('7k/8/8/8/8/8/q7/4K3 w - - 100 1');
 const out=await analyze({path:native,kind:'chesslab',sha256:hash},r,{kind:'depth',depth:1});
 assert.equal(out.result.decision.kind,'claim_draw');assert.equal(out.record.moves.length,0);assert.equal(out.identity.effective.MultiPV,'1');
});
test('invalid FENs and counter exhaustion match ChessCore validation boundaries',()=>{
 const invalid=[
  '8/8/8/8/8/8/4k3/4K3 w - - 0 1',
  '4k3/8/8/8/8/8/8/4K3 w K - 0 1',
  '4k3/8/8/8/8/8/8/4K3 w - d6 0 1',
  '4k3/8/8/3pP3/8/8/8/4K3 w - d6 1 1',
  '4k3/8/8/8/8/8/8/4K3 w - - 4294967296 1',
  '4k3/8/8/8/8/8/8/4K3 w - - 0 4294967296',
  '4k3/8/8/8/8/8/4Q3/4K3 w - - 0 1'
 ];
 for(const fen of invalid) {
  assert.throws(()=>new FideRules(record(fen)),fen);
  const c=JSON.parse(execFileSync(resolve(build,'m6_rules_probe'),[],{input:JSON.stringify(record(fen))+'\n',encoding:'utf8',timeout:2000}));assert(c.error,fen);
 }
 const r=new FideRules(record('7k/8/8/8/8/8/q7/4K3 w - - 4294967295 1'));
 const before=r.fen();assert.throws(()=>r.play('e1d1'),/counter/);assert.equal(r.fen(),before);
});
test('superseded game failure never accepts a later completed engine proposal',async t=>{
 const e=await instance(t,'stop-late',{stopGraceMs:1000}),g=new Game(initial),req=request(g),p=e.search(req);
 e.stop();g.fail('explicitly superseded');const result=await p;assert.throws(()=>g.accept(result),/stale/);assert.equal(g.record().moves.length,0);assert.equal(e.state,'READY');
 const fresh=new Game(initial);const next=e.search({...request(fresh),stopAfterMs:0});await next;
});
test('stop grace gives no clock subsidy and unverified claim cannot stop time',()=>{
 const v=virtual(),g=new Game(initial,{kind:'fischer',initialMs:10,incrementMs:100},v.clock);
 const p=proposal(g,'e2e4',5,{kind:'claim_draw'});v.set(20);assert.throws(()=>g.accept(p),/claim/);assert.equal(g.available(),0);assert.equal(g.clocks().w,10);
 assert.equal(g.flag().kind,'adjudication_required');assert.equal(g.clocks().w,0);
});
test('FIDE timeout win/draw requires certified possibility/impossibility',()=>{
 const v=virtual(),draw=new Game(record('7k/8/5KQ1/8/8/8/8/8 w - - 0 1'),{kind:'fischer',initialMs:1,incrementMs:0},v.clock);v.set(1);
 assert.equal(draw.flag().result,'1/2-1/2');
 const v2=virtual(),win=new Game(record('7k/8/5K2/6Q1/8/8/8/8 b - - 0 1'),{kind:'fischer',initialMs:1,incrementMs:0},v2.clock);v2.set(1);
 assert.equal(win.flag().result,'1-0');assert.equal(win.events.at(-1).proof.status,'CAN_MATE');
});
