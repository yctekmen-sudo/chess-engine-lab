import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,rm,readFile,writeFile,unlink,mkdir,readdir} from 'node:fs/promises';
import {tmpdir,hostname} from 'node:os';
import {resolve,join,relative} from 'node:path';
import {execFileSync} from 'node:child_process';
import {randomUUID} from 'node:crypto';
import {createExperiment,resumeExperiment,experimentStatus,exportExperiment,schedule,validateConfig,readRecord,writeRecord,writerLock} from '../dist/index.js';
import {nativeEngine as native} from './native-paths.mjs';
const mock=resolve(import.meta.dirname,'mock.mjs');
const config=(mode='normal',extra={})=>({engines:[{path:native},{path:process.execPath,args:[mock,mode],handshakeMs:1000,stopGraceMs:50}],openings:[{initialFen:'7k/8/5KQ1/8/8/8/8/8 w - - 149 1',moves:[],complete:false}],seed:13,limit:{kind:'depth',depth:1},time:{kind:'untimed'},maxPlies:4,searchCapMs:2000,...extra});
async function dir(t){const d=await mkdtemp(join(tmpdir(),'m6-experiment-'));t.after(()=>rm(d,{recursive:true,force:true}));return d;}
test('seeded paired schedule swaps colors and preserves identical opening conditions',()=>{
 const c=validateConfig(config());assert.deepEqual(schedule(c),schedule(c));const [a,b]=schedule(c);
 assert.equal(a.white,b.black);assert.equal(a.black,b.white);assert.deepEqual(a.opening,b.opening);assert.equal(a.pair,b.pair);
 assert.throws(()=>validateConfig({...config(),limit:{kind:'nodes',nodes:1}}));
});
test('real native/mock paired match, durable identities, PGN, idempotent resume',async t=>{
 const d=await dir(t),m=await createExperiment(d,config());assert.equal(m.identities.length,2);
 const s=await resumeExperiment(d);assert.equal(s.completed,2);assert.equal(s.incompletePairs.length,0);assert.equal(s.counts.invalid,0);assert.equal(s.counts.technicalForfeits,0);
 const before=await readFile(join(d,'games/game-0-0.json'),'utf8'),again=await resumeExperiment(d);assert.equal(again.attempts.length,2);assert.equal(await readFile(join(d,'games/game-0-0.json'),'utf8'),before);
 const out=await exportExperiment(d);assert.match(out.pgn,/\[FEN /);assert.match(out.pgn,/\[Result "(?:1-0|1\/2-1\/2)"\]/);
 assert.equal(out.sidecar.attempts.length,2);assert(m.artifacts[0].length>=2);assert.match(m.identities[0].m2[0],/artifact_sha256/);
});
test('technical forfeit is separate from chess WDL and invalid-game policy',async t=>{
 for(const failurePolicy of ['technical_forfeit','invalid']) {
  const d=await dir(t);await createExperiment(d,config('crash',{failurePolicy}));const s=await resumeExperiment(d);
  assert.equal(s.completed,2);assert.equal(failurePolicy==='invalid'?s.counts.invalid:s.counts.technicalForfeits,1);
  const out=await exportExperiment(d);const technical=out.sidecar.attempts.find(a=>a.outcome.kind===failurePolicy);assert(technical);
  assert.equal(technical.record.moves.length,0);assert(out.pgn.includes('[Result "*"]'));
 }
});
test('writer lock conflicts, explicit stale recovery and live-owner refusal',async t=>{
 const d=await dir(t),release=await writerLock(d);await assert.rejects(writerLock(d),/lock exists/);await assert.rejects(writerLock(d,true),/alive/);await release();
 await writeRecord(join(d,'writer.lock'),{host:hostname(),pid:2147483647,token:'stale'});
 await assert.rejects(writerLock(d),/explicit/);const unlock=await writerLock(d,true);await unlock();
});
test('torn records detected, interrupted attempts preserved and restarted from opening',async t=>{
 const d=await dir(t);await createExperiment(d,config());const id='attempt-'+randomUUID(),a=join(d,'attempts',id);await mkdir(a,{recursive:true});
 await writeRecord(join(a,'start.json'),{id,game:'game-0-0'});await writeFile(join(a,'state.json'),'{torn');
 const status=await resumeExperiment(d);assert.equal(status.completed,2);assert.equal(status.attempts.length,3);
 const interrupted=await readRecord(join(a,'interrupted.json'));assert.match(interrupted.reason,/torn/);
 assert((await readRecord(join(d,'attempts',status.attempts.find(n=>n!==id),'start.json'))).opening.moves.length===0);
 await writeFile(join(d,'summary.json'),'{torn');assert.equal((await experimentStatus(d)).completed,2);
 const official=join(d,'games/game-0-0.json'),saved=await readFile(official);await writeFile(official,'{torn');await assert.rejects(experimentStatus(d),/torn/);await writeFile(official,saved);
});
test('crash between immutable final and official publication recovers exactly once',async t=>{
 const d=await dir(t);await createExperiment(d,config());await resumeExperiment(d);
 const path=join(d,'games/game-0-0.json'),saved=await readFile(path,'utf8');await unlink(path);
 const s=await resumeExperiment(d);assert.equal(s.attempts.length,2);assert.equal(await readFile(path,'utf8'),saved);
});
test('immutable records reject replacement and corrupt manifest is never counted',async t=>{
 const d=await dir(t),p=join(d,'one.json');await writeRecord(p,{a:1});await assert.rejects(writeRecord(p,{a:2}),{code:'EEXIST'});assert.deepEqual(await readRecord(p),{a:1});
 await createExperiment(d,config());const p2=join(d,'manifest.json');const value=JSON.parse(await readFile(p2,'utf8'));value.data.config.seed++;
 await writeFile(p2,JSON.stringify(value));await assert.rejects(resumeExperiment(d),/integrity/);
});
test('resume refuses changed argv artifact, missing executable and config identity',async t=>{
 const d=await dir(t),script=join(d,'fixture.mjs');await writeFile(script,await readFile(mock));
 // An argv file outside controller cannot resolve chess.js: use self-contained UCI
 // fixture for identity setup, without a search in this check.
 await writeFile(script,"import{createInterface}from'node:readline';createInterface({input:process.stdin}).on('line',s=>{if(s==='uci')console.log('id name fixture\\nuciok');if(s==='isready')console.log('readyok');});");
 const c=config();c.engines[1]={path:process.execPath,args:[script]};await createExperiment(d,c);await writeFile(script,'changed');await assert.rejects(resumeExperiment(d));
});
test('actual CLI analyze, paired match, inspect, export, and repeated resume',async t=>{
 const d=await dir(t),cfg=join(d,'config.json'),batch=join(d,'batch'),cli=resolve(import.meta.dirname,'../dist/cli.js');await writeFile(cfg,JSON.stringify(config()));
 const invoke=(...args)=>JSON.parse(execFileSync(process.execPath,[cli,...args],{encoding:'utf8',timeout:15000,maxBuffer:2*1024*1024}));
 const a=invoke('analyze',cfg);assert.match(a.result.bestmove,/^[a-h]/);
 assert.equal(invoke('match',cfg,batch).completed,2);assert.equal(invoke('experiment','status',batch).completed,2);
 assert.equal(invoke('experiment','resume',batch).attempts.length,2);invoke('experiment','export',batch,join(d,'export'));assert((await readdir(d)).includes('export.pgn'));
});
test('automated unresolved flag stops pair without fabricating result or forfeit',async t=>{
 const d=await dir(t);await createExperiment(d,config('normal',{openings:[{initialFen:'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',moves:[],complete:true}],time:{kind:'fischer',initialMs:0,incrementMs:0}}));
 const s=await resumeExperiment(d);assert.equal(s.completed,1);assert.equal(s.counts.unresolved,1);assert.equal(s.counts.technicalForfeits,0);assert.deepEqual(s.counts.chess,{white:0,black:0,draw:0});assert.equal(s.incompletePairs.length,1);
 const out=await exportExperiment(d);assert(out.pgn.includes('[Result "*"]'));
});
test('startup forfeits name actual failed engine/color across paired colors and opening turns',async t=>{
 for(const turn of ['w','b'])for(const failurePolicy of ['technical_forfeit','invalid']) {
  const d=await dir(t),marker=relative(process.cwd(),join(d,'starts'));
  const c=config('startup-fail',{failurePolicy,openings:[{initialFen:`rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR ${turn} KQkq - 0 1`,moves:[],complete:false}]});
  c.engines[1].args.push(marker);await createExperiment(d,c);
  const status=await resumeExperiment(d);const exported=await exportExperiment(d);
  assert.equal(status.completed,2);assert.deepEqual(status.counts.chess,{white:0,black:0,draw:0});
  assert.deepEqual(status.counts.matchPoints,failurePolicy==='technical_forfeit'?[2,0]:[0,0]);
  for(const a of exported.sidecar.attempts){const expected=a.game==='game-0-0'?'b':'w';assert.equal(a.outcome.kind,failurePolicy);assert.equal(a.outcome.loser,expected);assert.match(a.outcome.reason,new RegExp(`engine=1 color=${expected}`));assert.equal(a.record.moves.length,0);}
 }
});
test('neutral startup identity drift invalidates games without assigning a forfeit',async t=>{
 const d=await dir(t),c=config('identity-drift');c.engines[1].args.push(relative(process.cwd(),join(d,'starts')));
 await createExperiment(d,c);const s=await resumeExperiment(d),out=await exportExperiment(d);
 assert.equal(s.counts.invalid,2);assert.equal(s.counts.technicalForfeits,0);assert.deepEqual(s.counts.matchPoints,[0,0]);
 for(const a of out.sidecar.attempts){assert.equal(a.outcome.kind,'invalid');assert(!('loser' in a.outcome));assert.match(a.outcome.reason,/orchestration failure.*identity changed/);assert.equal(a.events.at(-1).kind,'orchestration_failure');}
});
test('fixed-depth CLI analysis uses searchCapMs and exits on a hung UCI engine',async t=>{
 const d=await dir(t),cfg=join(d,'config.json'),c=config('hang',{searchCapMs:10});c.engines.reverse();await writeFile(cfg,JSON.stringify(c));
 assert.throws(()=>execFileSync(process.execPath,[resolve(import.meta.dirname,'../dist/cli.js'),'analyze',cfg],{encoding:'utf8',timeout:5000,stdio:['ignore','pipe','pipe']}),e=>e.status===1&&e.signal===null&&/stop grace expired/.test(e.stderr));
});
test('Fischer clock-mode paired matches send standard clock fields on every search',async t=>{
 const d=await dir(t),log=join(d,'clock-go.txt'),c=config('clock-log',{limit:{kind:'clock'},time:{kind:'fischer',initialMs:5000,incrementMs:41}});
 c.engines[1].args.push(relative(process.cwd(),log));await createExperiment(d,c);const s=await resumeExperiment(d);
 assert.equal(s.completed,2);assert.equal(s.counts.technicalForfeits,0);assert.equal(s.counts.invalid,0);
 const lines=(await readFile(log,'utf8')).trim().split('\n');assert(lines.length>=1);
 for(const line of lines){const m=/^go wtime (\d+) btime (\d+) winc 41 binc 41$/.exec(line);assert(m,line);assert(Number(m[1])>=0&&Number(m[1])<=5041);assert(Number(m[2])>=0&&Number(m[2])<=5041);}
 assert.throws(()=>validateConfig(config('normal',{limit:{kind:'clock'},time:{kind:'untimed'}})),/requires Fischer/);
});
