import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,rm,readFile,writeFile,mkdir,unlink} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve,relative} from 'node:path';
import {randomUUID} from 'node:crypto';
import {pathToFileURL} from 'node:url';
import {readFileSync} from 'node:fs';
import {createExperiment,resumeExperiment,exportExperiment,validateConfig,readRecord,writeRecord,reportedStats,ProtocolCapture,SearchEvidenceCapture,ManagedEngine,Game,DEFAULT_POSITION,digest} from '../dist/index.js';
import {nativeEngine} from './native-paths.mjs';

const initial={initialFen:DEFAULT_POSITION,moves:[],complete:true};
const mock=resolve(import.meta.dirname,'mock.mjs');
const engine=(mode='diagnostics',log)=>({path:process.execPath,args:[mock,mode,...(log?[relative(process.cwd(),log)]:[])],handshakeMs:3000,stopGraceMs:300});
const config=(extra={})=>({engines:[engine(),engine('diagnostics-missing')],openings:[initial],limit:{kind:'depth',depth:1},maxPlies:2,searchCapMs:2000,...extra});
async function directory(t){const d=await mkdtemp(join(tmpdir(),'m71-'));t.after(()=>rm(d,{recursive:true,force:true}));return d;}
async function run(t,extra={}){const d=await directory(t);await createExperiment(d,config(extra));await resumeExperiment(d);return {d,out:(await exportExperiment(d)).sidecar};}

test('shared-limit configs remain unchanged; independent limits validate depth, movetime and mixed clocks',()=>{
 const legacy=validateConfig(config());assert(!('engineLimits' in legacy));assert(!('uciDiagnostics' in legacy));
 for(const engineLimits of [
  [{kind:'depth',depth:8},{kind:'depth',depth:3}],
  [{kind:'depth',depth:64},{kind:'movetime',ms:0}],
  [{kind:'movetime',ms:2147483647},{kind:'clock'}],
 ])assert.deepEqual(validateConfig(config({engineLimits,time:{kind:'fischer',initialMs:1000,incrementMs:3}})).engineLimits,engineLimits);
 for(const bad of [{kind:'depth',depth:0},{kind:'depth',depth:65},{kind:'depth',depth:1.5},{kind:'depth',depth:'8'},{kind:'movetime',ms:-1},{kind:'movetime',ms:1.5},{kind:'movetime',ms:2147483648},{kind:'movetime',ms:NaN},{kind:'infinite'}]) {
  for(const i of [0,1]){const limits=[{kind:'depth',depth:1},{kind:'movetime',ms:20}];limits[i]=bad;assert.throws(()=>validateConfig(config({engineLimits:limits})),new RegExp(`engineLimits\\[${i}\\]`));}
 }
 for(const engineLimits of [null,[],[{kind:'depth',depth:1}],[{kind:'depth',depth:1},{kind:'depth',depth:1},{}]])assert.throws(()=>validateConfig(config({engineLimits})),/exactly two/);
 assert.throws(()=>validateConfig(config({engineLimits:[{kind:'depth',depth:1},{kind:'clock'}]})),/requires Fischer/);
 for(const uciDiagnostics of [{maxEntries:0},{maxEntries:65537},{maxBytes:0},{maxBytes:8388609}])assert.throws(()=>validateConfig(config({uciDiagnostics})));
 assert.deepEqual(validateConfig(config({uciDiagnostics:{}})).uciDiagnostics,{maxEntries:4096,maxBytes:1048576});
});

test('engines receive different go depths and retain identity-bound settings across paired colors; traces match actual commands',async t=>{
 const d=await directory(t),logs=[join(d,'a.log'),join(d,'b.log')],engineLimits=[{kind:'depth',depth:8},{kind:'depth',depth:3}];
 const manifest=await createExperiment(d,config({engines:logs.map(log=>engine('diagnostics',log)),engineLimits,uciDiagnostics:{}}));
 const saved=await readFile(join(d,'manifest.json'),'utf8');await resumeExperiment(d);
 const {sidecar,pgn}=await exportExperiment(d);assert.equal(sidecar.attempts.length,2);assert.match(pgn,/\[Round "game-0-1"\]/);
 for(const attempt of sidecar.attempts){
  const schedule=sidecar.scheduled.find(g=>g.id===attempt.game);assert.equal(attempt.searches.length,2);
  assert(attempt.protocol.complete);assert(!attempt.protocol.truncated);
  assert.deepEqual(attempt.protocol.entries.map(e=>e.sequence),attempt.protocol.entries.map((_,i)=>i+1));
  for(const search of attempt.searches){
   assert.equal(search.engine,search.color==='w'?schedule.white:schedule.black);
   assert.deepEqual(search.requested,engineLimits[search.engine]);assert.equal(search.status,'completed');assert(search.accepted);
   assert.deepEqual(search.reported,{depth:2,nodes:123,timeMs:7,nps:17571});assert(search.measuredElapsedMs>=0);assert(Number.isFinite(search.measuredElapsedMs));
   const lines=attempt.protocol.entries.filter(e=>e.session===search.session);
   assert(lines.every(e=>e.engine===search.engine&&e.color===search.color&&e.game===attempt.game&&e.attempt===attempt.id&&e.ply===search.ply));
   const go=lines.findIndex(e=>e.direction==='out'&&e.line===`go depth ${engineLimits[search.engine].depth}`);
   const position=lines.findIndex(e=>e.direction==='out'&&e.line.startsWith('position '));
   const response=lines.findIndex(e=>e.direction==='in'&&e.line===`info string received go depth ${engineLimits[search.engine].depth}`);
   const best=lines.findIndex(e=>e.direction==='in'&&e.line.startsWith('bestmove '));assert(position>=0&&position<go&&go<response&&response<best);
  }
  for(const index of [0,1]){const traffic=attempt.protocol.entries.filter(e=>e.engine===index);assert(traffic.some(e=>e.line==='uci'));assert(traffic.some(e=>e.line==='ucinewgame'));assert.equal(traffic.at(-1).line,'quit');assert(!traffic.at(-1).session);}
 }
 for(const index of [0,1]){
  const received=(await readFile(logs[index],'utf8')).trim().split('\n');
  const traced=sidecar.attempts.flatMap(a=>a.protocol.entries.filter(e=>e.engine===index&&e.direction==='out').map(e=>e.line));
  // Preflight creates two earlier processes; the attempt suffix is exact.
  assert.deepEqual(received.slice(-traced.length),traced);
  assert.deepEqual(received.filter(s=>s.startsWith('go ')),Array(2).fill(`go depth ${engineLimits[index].depth}`));
 }
 assert.equal(manifest.identities.length,2);assert.equal(sidecar.searchSummary[0].reported.depth.min,2);
 await resumeExperiment(d);assert.equal((await exportExperiment(d)).sidecar.attempts.length,2);assert.equal(await readFile(join(d,'manifest.json'),'utf8'),saved);
});

test('mixed depth/movetime commands and missing optional per-move stats stay distinct from zero',async t=>{
 const {out}=await run(t,{engineLimits:[{kind:'depth',depth:4},{kind:'movetime',ms:250}],uciDiagnostics:{}});
 for(const a of out.attempts)for(const s of a.searches){
  assert(a.protocol.entries.some(e=>e.session===s.session&&e.line===(s.engine===0?'go depth 4':'go movetime 250')));
  assert.deepEqual(s.reported,s.engine===0?{depth:2,nodes:123,timeMs:7,nps:17571}:{depth:3});
  assert.notEqual(s.requested.kind==='depth'?s.requested.depth:undefined,s.reported.depth);
 }
 assert.deepEqual(out.searchSummary[1].reported.nodes,{samples:0});
});

test('mixed clock and depth modes use live Fischer fields with independent identity selection',async t=>{
 const {out}=await run(t,{engineLimits:[{kind:'clock'},{kind:'depth',depth:3}],time:{kind:'fischer',initialMs:10000,incrementMs:41},uciDiagnostics:{}});
 for(const a of out.attempts)for(const s of a.searches){
  const go=a.protocol.entries.find(e=>e.session===s.session&&e.line.startsWith('go ')).line;
  if(s.engine===0){assert.match(go,/^go wtime \d+ btime \d+ winc 41 binc 41$/);assert.equal(s.requested.kind,'clock');assert(s.requested.wtime<=10041);}
  else assert.equal(go,'go depth 3');
 }
});

test('protocol logging is disabled by default and legacy manifests resume/export without new fields',async t=>{
 const {d,out}=await run(t);for(const a of out.attempts){assert(!('protocol' in a));for(const s of a.searches)assert.deepEqual(s.requested,{kind:'depth',depth:1});}
 // Emulate an M6 finalized attempt without additive M7.1 search evidence.
 const a=out.attempts[0],path=join(d,'attempts',a.id,'final.json');delete a.searches;delete a.searchEvidence;await unlink(path);await writeRecord(path,a);
 // Emulate the earlier M7.1 array-only format in the other official attempt.
 const b=out.attempts[1],other=join(d,'attempts',b.id,'final.json');delete b.searchEvidence;await unlink(other);await writeRecord(other,b);
 const before=await readFile(path,'utf8');await resumeExperiment(d);assert.equal(await readFile(path,'utf8'),before);
 const legacy=(await exportExperiment(d)).sidecar;assert.equal(legacy.attempts.length,2);
 for(const summary of legacy.searchSummary){assert.equal(summary.searches,1);assert.equal(summary.attemptsWithoutSearchEvidence,1);}
});

test('entry and byte limits retain ordered prefixes and explicitly report truncation',async t=>{
 for(const uciDiagnostics of [{maxEntries:12,maxBytes:1048576},{maxEntries:4096,maxBytes:20}]){
  const {out}=await run(t,{engines:[engine('diagnostics-flood'),engine()],uciDiagnostics});
  for(const a of out.attempts){const p=a.protocol;assert(p.truncated);assert(!p.complete);assert(p.lifecycleComplete);assert(p.droppedEntries>0);assert.equal(p.observedEntries,p.entries.length+p.droppedEntries);assert(p.entries.length<=uciDiagnostics.maxEntries);assert(p.retainedBytes<=uciDiagnostics.maxBytes);}
 }
 const capture=new ProtocolCapture(1,1000);const entry={sequence:1,generation:'p',at:1,direction:'out',line:'uci',game:'g',attempt:'a',engine:0,color:'w'};
 capture.add(entry);const checkpoint=capture.snapshot(false);capture.add({...entry,line:'isready'});
 assert(!checkpoint.complete);assert(!checkpoint.lifecycleComplete);assert.equal(checkpoint.entries.length,1);assert.equal(capture.snapshot(true).droppedEntries,1);
});

test('raw search evidence is bounded separately from protocol capture',async t=>{
 const {out}=await run(t,{engines:[engine('diagnostics-flood'),engine()]});
 for(const a of out.attempts){const s=a.searches.find(s=>s.engine===0);assert.equal(s.info.length,4);assert.equal(s.infoDropped,37);assert.equal(s.reported.nodes,39);assert(!('protocol' in a));}
});

test('search-cap stop captures ordered stop/response and marks the result interrupted',async t=>{
 const {out}=await run(t,{engines:[engine('diagnostics-stop'),engine()],searchCapMs:30,uciDiagnostics:{}});
 for(const a of out.attempts){const s=a.searches.find(s=>s.engine===0);assert.equal(s.status,'interrupted');assert(s.stopRequested);assert(s.accepted);
  const lines=a.protocol.entries.filter(e=>e.session===s.session);const stop=lines.findIndex(e=>e.direction==='out'&&e.line==='stop'),best=lines.findIndex(e=>e.direction==='in'&&e.line.startsWith('bestmove '));assert(stop>=0&&best>stop);assert(s.measuredElapsedMs>=20);
 }
});

test('failed searches never look complete and cannot produce accepted move statistics',async t=>{
 const {out}=await run(t,{engines:[engine('hang'),engine()],searchCapMs:20,uciDiagnostics:{}});
 for(const a of out.attempts){const s=a.searches.find(s=>s.engine===0);assert.equal(s.status,'failed');assert(!s.accepted);assert(s.stopRequested);assert.deepEqual(s.reported,{});assert(!a.protocol.complete);assert(!a.protocol.lifecycleComplete);}
});

test('interrupted attempt export retains durable evidence and explicitly labels checkpoint coverage',async t=>{
 const d=await directory(t);await createExperiment(d,config());const id='attempt-'+randomUUID(),path=join(d,'attempts',id);await mkdir(path,{recursive:true});
 await writeRecord(join(path,'start.json'),{id,game:'game-0-0'});await writeRecord(join(path,'state.json'),{record:initial,searches:[],protocol:{complete:false,entries:[]}});
 const before=(await exportExperiment(d)).sidecar;assert.equal(before.attempts.length,0);assert.equal(before.incompleteAttempts[0].complete,false);assert.equal(before.incompleteAttempts[0].status,'unfinished');assert.match(before.incompleteAttempts[0].evidenceThrough,/checkpoint/);
 await resumeExperiment(d);const after=(await exportExperiment(d)).sidecar;assert.equal(after.attempts.length,2);assert.equal(after.incompleteAttempts.length,1);assert.equal(after.incompleteAttempts[0].status,'interrupted');
 await writeFile(join(path,'state.json'),'{corrupt');await assert.rejects(exportExperiment(d),/torn/);
});

test('standard info parsing rejects malformed counters and ignores string/PV payload tokens',()=>{
 assert.deepEqual(reportedStats('info depth 4 nodes 0 time 0 nps 0'),{depth:4,nodes:0,timeMs:0,nps:0});
 assert.deepEqual(reportedStats('info depth -1 nodes 9007199254740992 time nope nps NaN'),{});
 assert.deepEqual(reportedStats('info depth 4 pv a2a3 nodes 999 time 88'),{depth:4});
 assert.equal(reportedStats('info string nodes 12 depth 99'),undefined);
 assert.deepEqual(reportedStats('info nodes invalid',{depth:2,nodes:123}),{depth:2});
 assert.deepEqual(reportedStats('info constructor 1 toString 2'),{});
});

test('throwing protocol observers preserve repeated searches but permanently invalidate capture completeness',async t=>{
 for(const failLine of ['uci','info depth 2 nodes 123 time 7 nps 17571 pv a2a3','quit']) {
  const capture=new ProtocolCapture(4096,1048576),game=new Game(initial);let thrown=false;
  const e=new ManagedEngine(engine(),undefined,line=>{
   if(!thrown&&line.line===failLine){thrown=true;throw Error('observer failure');}
   capture.add({...line,game:game.id,attempt:'observer-test',engine:0,color:game.side()});
  });t.after(()=>e.close());await e.start();
  for(let i=0;i<2;i++){
   const session=randomUUID();const proposal=await e.search({session,binding:game.begin(session),record:game.record(),limit:{kind:'depth',depth:1}});
   game.accept(proposal);assert.equal(e.state,'READY');assert.equal(e.lastSearchEvidence.status,'completed');
  }
  assert.equal(game.record().moves.length,2);await e.close();assert(e.transcript.includes('> quit'));
  assert(thrown);assert.equal(e.protocolObserverFailures,1);assert(!e.protocolComplete);
  const evidence=capture.snapshot(e.protocolComplete,e.protocolObserverFailures);
  assert(!evidence.complete);assert.equal(evidence.observerFailures,1);assert(!evidence.truncated);assert.equal(evidence.droppedEntries,0);
  assert(!capture.snapshot(true,e.protocolObserverFailures).complete);
 }
});

test('experiment export discloses observer failures without changing accepted engine moves',async t=>{
 const add=ProtocolCapture.prototype.add;
 t.mock.method(ProtocolCapture.prototype,'add',function(line){
  if(line.line==='go depth 1')throw Error('intentional capture observer failure');
  return add.call(this,line);
 });
 const {out}=await run(t,{uciDiagnostics:{}});
 for(const a of out.attempts){
  assert.equal(a.record.moves.length,2);assert(a.searches.every(s=>s.accepted&&s.status==='completed'));
  assert.equal(a.protocol.observerFailures,2);assert(!a.protocol.complete);assert(!a.protocol.lifecycleComplete);
  assert(!a.protocol.truncated);assert.equal(a.protocol.droppedEntries,0);
 }
});

test('native Chess Lab search exposes reported stats and exact outgoing depth evidence',async t=>{
 const {out}=await run(t,{engines:[{path:nativeEngine},engine()],engineLimits:[{kind:'depth',depth:1},{kind:'depth',depth:3}],uciDiagnostics:{}});
 for(const a of out.attempts){const s=a.searches.find(s=>s.engine===0);assert(s.accepted);assert.equal(s.reported.depth,1);assert(s.reported.nodes>0);assert(a.protocol.entries.some(e=>e.session===s.session&&e.direction==='out'&&e.line==='go depth 1'));}
});

test('diagnostic evidence stays inside record integrity and engine artifact verification',async t=>{
 const {d,out}=await run(t,{uciDiagnostics:{},engineLimits:[{kind:'depth',depth:8},{kind:'depth',depth:3}]});
 const path=join(d,'attempts',out.attempts[0].id,'final.json'),envelope=JSON.parse(await readFile(path,'utf8'));
 envelope.data.protocol.entries[0].line='tampered';await writeFile(path,JSON.stringify(envelope));await assert.rejects(exportExperiment(d),/integrity/);
 const fresh=await directory(t),script=join(fresh,'engine.mjs');await writeFile(script,`import ${JSON.stringify(pathToFileURL(mock).href)};`);
 await createExperiment(fresh,config({engines:[{path:process.execPath,args:[script,'diagnostics']},engine()],uciDiagnostics:{}}));
 await writeFile(script,`import ${JSON.stringify(pathToFileURL(mock).href)}; // changed artifact`);
 await assert.rejects(resumeExperiment(fresh),/identity changed/);
});

const searchSample=i=>({session:`search-${i}`,engine:i%2,color:i%2?'b':'w',ply:i+1,accepted:true,requested:{kind:'depth',depth:8},startedAt:i*3,finishedAt:i*3+2,measuredElapsedMs:2,status:'completed',stopRequested:false,reported:i%2?{depth:3}:{depth:4,nodes:i},info:[{line:'info depth 4 '+ 'x'.repeat(512),at:i*3+1,truncated:false}],infoDropped:0});

test('100000 search observations retain bounded details and checkpoint size with full aggregate coverage',()=>{
 const capture=new SearchEvidenceCapture(4,16384);let checkpoint;
 for(let i=0;i<100000;i++){capture.add(searchSample(i));if(i===999)checkpoint=capture.snapshot(false);}
 const {searches,searchEvidence:evidence}=capture.snapshot(true);
 assert.equal(searches.length,4);assert.deepEqual(searches.map(s=>s.session),['search-0','search-1','search-2','search-3']);
 assert.equal(evidence.observedSearches,100000);assert.equal(evidence.droppedSearches,99996);assert(evidence.truncated);assert(!evidence.complete);assert(evidence.lifecycleComplete);
 assert.equal(evidence.retainedBytes,searches.reduce((n,s)=>n+Buffer.byteLength(JSON.stringify(s)),0));assert(evidence.retainedBytes<=16384);
 assert.deepEqual(evidence.firstDropped,{session:'search-4',engine:0,color:'w',ply:5});assert.deepEqual(evidence.lastDropped,{session:'search-99999',engine:1,color:'b',ply:100000});
 for(const total of evidence.summary){assert.equal(total.searches,50000);assert.equal(total.completed,50000);assert.equal(total.measuredElapsedMs,100000);}
 assert.deepEqual(evidence.summary[0].reported.nodes,{samples:50000,min:0,max:99998});assert.deepEqual(evidence.summary[1].reported.nodes,{samples:0});
 assert.equal(checkpoint.searchEvidence.observedSearches,1000);assert.equal(checkpoint.searchEvidence.retainedBytes,evidence.retainedBytes);
 assert(JSON.stringify(capture.snapshot(true)).length<16384+4096);
 assert(Math.abs(JSON.stringify(capture.snapshot(true)).length-JSON.stringify(checkpoint).length)<512);
});

test('byte exhaustion is disclosed and saturated capture does not serialize omitted raw search info',()=>{
 const first=searchSample(0),capture=new SearchEvidenceCapture(10,Buffer.byteLength(JSON.stringify(first)));
 capture.add(first);const checkpoint=capture.snapshot(false);first.info[0].line='mutated';
 const failed={...searchSample(1),status:'failed',accepted:false};capture.add(failed);
 // Serialization is deliberately unusable after saturation. Counters and
 // attribution must still update without walking this omitted raw evidence.
 capture.add({...searchSample(2),status:'interrupted',toJSON(){throw Error('raw evidence serialized after saturation');}});
 const snapshot=capture.snapshot(true);assert.equal(snapshot.searches.length,1);assert(snapshot.searchEvidence.truncated);assert(!snapshot.searchEvidence.complete);
 assert.equal(snapshot.searchEvidence.droppedSearches,2);assert.equal(snapshot.searchEvidence.summary[0].interrupted,1);assert.equal(snapshot.searchEvidence.summary[1].failed,1);
 assert.equal(checkpoint.searchEvidence.observedSearches,1);assert.notEqual(snapshot.searches[0].info[0].line,'mutated');
 assert(!checkpoint.searchEvidence.complete);assert(!checkpoint.searchEvidence.lifecycleComplete);
 const sufficient=new SearchEvidenceCapture(10,10000);sufficient.add(searchSample(0));assert(sufficient.snapshot(true).searchEvidence.complete);
});

test('search evidence budgets validate without changing old manifest configuration defaults',()=>{
 assert(!('searchEvidence' in validateConfig(config())));
 assert.deepEqual(validateConfig(config({maxPlies:100000,searchEvidence:{}})).searchEvidence,{maxEntries:256,maxBytes:262144});
 for(const searchEvidence of [{maxEntries:0},{maxEntries:4097},{maxEntries:1.5},{maxBytes:0},{maxBytes:4194305},{maxBytes:'1000'},null])assert.throws(()=>validateConfig(config({searchEvidence})));
});

test('bounded per-search checkpoints retain attribution, export all-search summaries and preserve immutable resume',async t=>{
 for(const limits of [{maxEntries:1,maxBytes:262144},{maxEntries:256,maxBytes:1}]){
  const d=await directory(t);await createExperiment(d,config({maxPlies:6,searchEvidence:limits}));const checkpoints=[];
  await resumeExperiment(d,{onProgress:value=>{
   const state=JSON.parse(readFileSync(join(d,'attempts',value.attempt,'state.json'),'utf8'));
   assert.equal(state.digest,digest(state.data));checkpoints.push(state.data);
  }});
  assert.equal(checkpoints.length,12);
  for(const state of checkpoints){assert(state.searches.length<=limits.maxEntries);assert(state.searchEvidence.retainedBytes<=limits.maxBytes);assert(!state.searchEvidence.complete);assert(!state.searchEvidence.lifecycleComplete);}
  const {sidecar:out,pgn}=await exportExperiment(d);assert.equal(out.attempts.length,2);assert.match(pgn,/\[Round "game-0-1"\]/);
  for(const a of out.attempts){
   const e=a.searchEvidence;assert.equal(e.observedSearches,6);assert.equal(e.droppedSearches,6-a.searches.length);assert(e.truncated);assert(!e.complete);assert(e.lifecycleComplete);
   assert.equal(a.searches.length,limits.maxBytes===1?0:1);
   const scheduled=out.scheduled.find(g=>g.id===a.game);
   if(a.searches.length){assert.equal(a.searches[0].engine,scheduled.white);assert.equal(a.searches[0].color,'w');assert.equal(a.searches[0].ply,1);}
   for(const total of e.summary){assert.equal(total.searches,3);assert.equal(total.completed,3);}
  }
  for(const total of out.searchSummary){assert.equal(total.searches,6);assert.equal(total.completed,6);assert.equal(total.droppedSearches,total.searches-total.retainedSearches);assert.equal(total.attemptsWithTruncatedSearchEvidence,2);}
  assert.deepEqual(out.searchSummary[0].reported.nodes,{samples:6,min:123,max:123});assert.deepEqual(out.searchSummary[1].reported.nodes,{samples:0});
  const a=out.attempts[0],path=join(d,'attempts',a.id,'final.json'),before=await readFile(path,'utf8');await resumeExperiment(d);
  assert.equal(await readFile(path,'utf8'),before);assert.equal((await exportExperiment(d)).sidecar.attempts.length,2);
  const envelope=JSON.parse(before);envelope.data.searchEvidence.summary[0].searches++;await writeFile(path,JSON.stringify(envelope));
  await assert.rejects(exportExperiment(d),/integrity/);
 }
});

test('interrupted bounded evidence recovers its last durable coverage and restarts with a fresh capture',async t=>{
 const d=await directory(t);await createExperiment(d,config({searchEvidence:{maxEntries:1,maxBytes:262144}}));
 const id='attempt-'+randomUUID(),dir=join(d,'attempts',id),capture=new SearchEvidenceCapture(1,262144);
 capture.add(searchSample(0));capture.add(searchSample(1));await writeRecord(join(dir,'start.json'),{id,game:'game-0-0'});
 await writeRecord(join(dir,'state.json'),{record:initial,...capture.snapshot(false)});const before=await readFile(join(dir,'state.json'),'utf8');
 await resumeExperiment(d);assert.equal(await readFile(join(dir,'state.json'),'utf8'),before);
 const out=(await exportExperiment(d)).sidecar,interrupted=out.incompleteAttempts.find(a=>a.id===id);
 assert.equal(interrupted.status,'interrupted');assert(!interrupted.state.searchEvidence.complete);assert(interrupted.state.searchEvidence.truncated);
 assert.equal(interrupted.state.searchEvidence.observedSearches,2);assert.equal(interrupted.state.searchEvidence.droppedSearches,1);
 for(const a of out.attempts){assert.notEqual(a.id,id);assert.equal(a.searchEvidence.observedSearches,2);assert.equal(a.searches[0].ply,1);}
});
