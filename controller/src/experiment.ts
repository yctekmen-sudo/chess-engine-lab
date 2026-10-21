import { readFile, stat } from 'node:fs/promises';
import { join, isAbsolute } from 'node:path';
import { createHash, randomUUID } from 'node:crypto';
import { hostname, platform, arch } from 'node:os';
import { execFileSync } from 'node:child_process';
import { ManagedEngine, executableIdentity, type EngineConfig, type EngineIdentity, type SearchLimit } from './engine.js';
import { Game, monotonic, type Outcome, type TimeControl } from './game.js';
import { FideRules, DEFAULT_POSITION, type RecordState } from './rules.js';
import { canonical, digest, exists, names, readRecord, writeRecord, writerLock } from './store.js';
export type MatchConfig={engines:[EngineConfig,EngineConfig];openings:RecordState[];seed:number;limit:Exclude<SearchLimit,{kind:'infinite'}>;time:TimeControl;failurePolicy:'technical_forfeit'|'invalid';maxPlies:number;searchCapMs:number};
export type ScheduledGame={id:string;pair:string;white:0|1;black:0|1;opening:RecordState};
type Artifact={path:string;sha256:string};
type Manifest={id:string;created:string;config:MatchConfig;identities:EngineIdentity[];artifacts:Artifact[][];scheduleDigest:string;runtime:{node:string;controller:string;platform:string;arch:string;host:string;compiler:string};policies:Record<string,string>};
type Attempt={id:string;game:string;record:RecordState;events:unknown[];clocks:Record<string,number|null>;outcome:Outcome;elapsedMs:number;progress:unknown[]};
type Official={game:string;attempt:string};
function object(v:unknown):Record<string,unknown> {if(!v||typeof v!=='object'||Array.isArray(v))throw Error('expected config object');return v as Record<string,unknown>;}
function int(v:unknown,lo:number,hi:number):number {if(typeof v!=='number'||!Number.isSafeInteger(v)||v<lo||v>hi)throw Error(`expected integer ${lo}..${hi}`);return v;}
export function validateConfig(value:unknown):MatchConfig {
  const v=object(value);if(!Array.isArray(v.engines)||v.engines.length!==2)throw Error('exactly two engines required');
  const engines=v.engines.map(raw=>{
    const e=object(raw);if(typeof e.path!=='string'||!isAbsolute(e.path))throw Error('absolute engine path required');
    if(e.kind!==undefined&&e.kind!=='uci'&&e.kind!=='chesslab')throw Error('invalid engine kind');
    if(e.sha256!==undefined&&(typeof e.sha256!=='string'||!/^[0-9a-f]{64}$/.test(e.sha256)))throw Error('invalid SHA-256');
    if(e.args!==undefined&&(!Array.isArray(e.args)||e.args.some(x=>typeof x!=='string')))throw Error('invalid argv');
    if(e.options!==undefined&&Object.values(object(e.options)).some(x=>typeof x!=='string'))throw Error('options must map names to strings');
    for(const k of ['handshakeMs','stopGraceMs'])if(e[k]!==undefined)int(e[k],1,60000);
    return structuredClone(e) as EngineConfig;
  }) as [EngineConfig,EngineConfig];
  const openings=v.openings??[{initialFen:DEFAULT_POSITION,moves:[],complete:true}];
  if(!Array.isArray(openings)||openings.length===0||openings.length>10000)throw Error('invalid openings');
  const records=openings.map(raw=>{
    const r=object(raw);if(typeof r.initialFen!=='string'||typeof r.complete!=='boolean'||!Array.isArray(r.moves)||r.moves.some(m=>typeof m!=='string'))throw Error('invalid opening history');
    const rules=new FideRules(r as RecordState);return rules.record();
  });
  const limit=object(v.limit);if(limit.kind==='depth')int(limit.depth,1,64);else if(limit.kind==='movetime')int(limit.ms,0,2147483647);else throw Error('match requires depth or movetime');
  const time=object(v.time??{kind:'untimed'});
  if(time.kind==='fischer'){int(time.initialMs,0,2147483647);int(time.incrementMs,0,2147483647);}else if(time.kind!=='untimed')throw Error('unsupported clock');
  if(v.failurePolicy!==undefined&&v.failurePolicy!=='technical_forfeit'&&v.failurePolicy!=='invalid')throw Error('invalid failure policy');
  return {engines,openings:records,seed:int(v.seed??1,0,0xffffffff),limit:limit as MatchConfig['limit'],time:time as TimeControl,failurePolicy:v.failurePolicy??'technical_forfeit',maxPlies:int(v.maxPlies??1000,1,100000),searchCapMs:int(v.searchCapMs??60000,1,2147483647)};
}
export function schedule(config:MatchConfig):ScheduledGame[] {
  const indices=config.openings.map((_,i)=>i);let state=config.seed>>>0;
  const random=()=>{state=(Math.imul(state,1664525)+1013904223)>>>0;return state;};
  for(let i=indices.length-1;i>0;i--){const j=random()%(i+1);[indices[i],indices[j]]=[indices[j]!,indices[i]!];}
  return indices.flatMap((opening,i)=>[0,1].map(swap=>({id:`game-${i}-${swap}`,pair:`pair-${i}`,white:swap as 0|1,black:(1-swap) as 0|1,opening:config.openings[opening]!})));
}
async function artifacts(config:EngineConfig,identity?:EngineIdentity):Promise<Artifact[]> {
  const id=await executableIdentity(config);const paths=new Set([id.path]);
  for(const p of [...(config.args??[]),identity?.effective.Evaluator??''])if(isAbsolute(p)) {
    try{if((await stat(p)).isFile())paths.add(p);}catch{throw Error('missing configured artifact '+p);}
  }
  const out:Artifact[]=[];for(const path of [...paths].sort())out.push({path,sha256:createHash('sha256').update(await readFile(path)).digest('hex')});return out;
}
async function inspect(config:EngineConfig):Promise<EngineIdentity> {const engine=new ManagedEngine(config);try{return await engine.start();}finally{await engine.close();}}
export async function createExperiment(directory:string,input:unknown):Promise<Manifest> {
  const config=validateConfig(input);const release=await writerLock(directory);
  try {
    if(await exists(join(directory,'manifest.json'))||await exists(join(directory,'schedule.json')))throw Error('experiment already exists');
    const identities:EngineIdentity[]=[],files:Artifact[][]=[];
    for(const e of config.engines){const identity=await inspect(e);identities.push(identity);files.push(await artifacts(e,identity));}
    const games=schedule(config);
    let revision='unknown',compiler='unknown';
    try{revision=execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8',stdio:['ignore','pipe','ignore']}).trim();}catch{/* source archive */}
    try{compiler=execFileSync('c++',['--version'],{encoding:'utf8',stdio:['ignore','pipe','ignore']}).split('\n')[0]!;}catch{/* external engines */}
    const manifest:Manifest={id:randomUUID(),created:new Date().toISOString(),config,identities,artifacts:files,scheduleDigest:digest(games),runtime:{node:process.version,controller:revision,platform:platform(),arch:arch(),host:hostname(),compiler},policies:{gameClock:'monotonic receipt < deadline; accepted move only increment',adapterClock:'min(remaining, remaining/30 + increment/2)',comparison:config.limit.kind,scoreUnits:'raw per evaluator; no cross-evaluator normalization',tt:'effective UCI options; no certification inferred',recovery:'fresh attempt from scheduled opening'}};
    await writeRecord(join(directory,'schedule.json'),games);await writeRecord(join(directory,'manifest.json'),manifest);return manifest;
  }finally{await release();}
}
async function load(directory:string):Promise<{manifest:Manifest;games:ScheduledGame[]}> {
  const manifest=await readRecord<Manifest>(join(directory,'manifest.json')),games=await readRecord<ScheduledGame[]>(join(directory,'schedule.json'));
  validateConfig(manifest.config);
  if(digest(games)!==manifest.scheduleDigest||canonical(schedule(manifest.config))!==canonical(games))throw Error('schedule mismatch');
  return {manifest,games};
}
async function completed(directory:string,games:ScheduledGame[]):Promise<Map<string,Attempt>> {
  const done=new Map<string,Attempt>();
  for(const g of games)if(await exists(join(directory,'games',g.id+'.json'))) {
    const official=await readRecord<Official>(join(directory,'games',g.id+'.json'));
    if(official.game!==g.id||!/^attempt-[0-9a-f-]+$/.test(official.attempt))throw Error('invalid official reference');
    const attempt=await readRecord<Attempt>(join(directory,'attempts',official.attempt,'final.json'));
    if(attempt.game!==g.id||attempt.id!==official.attempt)throw Error('attempt reference mismatch');
    new FideRules(attempt.record).assertReplay();done.set(g.id,attempt);
  }return done;
}
export async function experimentStatus(directory:string) {
  const {manifest,games}=await load(directory),done=await completed(directory,games);
  const counts={chess:{white:0,black:0,draw:0},technicalForfeits:0,invalid:0,unresolved:0,matchPoints:[0,0]};
  for(const g of games){const a=done.get(g.id);if(!a)continue;const o=a.outcome;
    if(o.kind==='chess') {
      if(o.result==='1/2-1/2'){counts.chess.draw++;counts.matchPoints[g.white]!+=0.5;counts.matchPoints[g.black]!+=0.5;}
      else{const side=o.result==='1-0'?'white':'black';counts.chess[side]++;counts.matchPoints[g[side]]!++;}
    }else if(o.kind==='technical_forfeit'){counts.technicalForfeits++;counts.matchPoints[o.loser==='w'?g.black:g.white]!++;}
    else if(o.kind==='invalid')counts.invalid++;else counts.unresolved++;
  }
  return {experiment:manifest.id,scheduled:games.length,completed:done.size,counts,incompletePairs:[...new Set(games.filter(g=>!done.has(g.id)).map(g=>g.pair))],attempts:await names(join(directory,'attempts'))};
}
async function finalize(directory:string,g:ScheduledGame,attempt:Attempt):Promise<void> {
  await writeRecord(join(directory,'attempts',attempt.id,'final.json'),attempt);
  await writeRecord(join(directory,'games',g.id+'.json'),{game:g.id,attempt:attempt.id});
}
export async function resumeExperiment(directory:string,options:{takeoverStaleLock?:boolean;onProgress?:(value:unknown)=>void}={}) {
  const release=await writerLock(directory,options.takeoverStaleLock??false);
  try {
    const {manifest,games}=await load(directory),config=manifest.config;
    for(let i=0;i<2;i++) {
      const identity=await inspect(config.engines[i]!);
      if(canonical(identity)!==canonical(manifest.identities[i])||canonical(await artifacts(config.engines[i]!,identity))!==canonical(manifest.artifacts[i]))throw Error('engine/config identity changed; cannot resume');
    }
    // A crash between final publication and official-reference publication must
    // recover that result, not run another game. Preserve unfinished attempts.
    for(const name of await names(join(directory,'attempts'))) {
      if(!/^attempt-[0-9a-f-]+$/.test(name))throw Error('invalid attempt directory');
      const dir=join(directory,'attempts',name),start=await readRecord<{game:string}>(join(dir,'start.json'));
      const g=games.find(g=>g.id===start.game);if(!g)throw Error('attempt outside schedule');
      if(await exists(join(dir,'final.json'))) {
        const a=await readRecord<Attempt>(join(dir,'final.json'));
        if(a.game!==g.id||a.id!==name)throw Error('invalid final identity');
        const official=join(directory,'games',g.id+'.json');
        if(!await exists(official))await writeRecord(official,{game:g.id,attempt:name});
        else if((await readRecord<Official>(official)).attempt!==name)throw Error('multiple finalized attempts for one game');
      }else if(!await exists(join(dir,'interrupted.json'))) {
        // Any corrupt incremental snapshot is reported, never counted as final.
        const snapshot=join(dir,'state.json');let diagnostic='interrupted before first state';
        if(await exists(snapshot))try{await readRecord(snapshot);diagnostic='interrupted; valid replay snapshot retained';}catch(e){diagnostic=String(e);}
        await writeRecord(join(dir,'interrupted.json'),{reason:diagnostic,restartedFrom:'original scheduled opening'});
      }
    }
    const done=await completed(directory,games);
    for(const g of games) {
      if(done.has(g.id))continue;
      const id='attempt-'+randomUUID(),dir=join(directory,'attempts',id),started=monotonic.now();
      await writeRecord(join(dir,'start.json'),{id,game:g.id,opening:g.opening,configDigest:digest(config)});
      let game=new Game(g.opening,config.time);const progress:unknown[]=[];
      const engines=[new ManagedEngine(config.engines[g.white]),new ManagedEngine(config.engines[g.black])];
      const snapshot=()=>({record:game.record(),events:game.events,clocks:{w:Number.isFinite(game.clocks().w)?game.clocks().w:null,b:Number.isFinite(game.clocks().b)?game.clocks().b:null}});
      try {
        for(let i=0;i<2;i++) {
          const identity=await engines[i]!.start(),index=i===0?g.white:g.black;
          if(canonical(identity)!==canonical(manifest.identities[index]))throw Error('engine identity changed at attempt start');
        }
        // Engine startup is setup time. Construct the actual game clock only
        // after both fresh processes have completed their handshake.
        game=new Game(g.opening,config.time);
        while(!game.outcome()) {
          if(game.record().moves.length-g.opening.moves.length>=config.maxPlies){game.fail('explicit maxPlies work limit','invalid');break;}
          if(game.available()<=0){game.flag();break;}
          const session=randomUUID(),binding=game.begin(session),engine=engines[game.side()==='w'?0:1]!;
          try {
            const p=await engine.search({session,binding,record:game.record(),limit:config.limit,stopAfterMs:Math.min(config.searchCapMs,game.available()),onProgress:p=>{progress.push(p);if(progress.length>128)progress.shift();}});
            game.accept(p);
          }catch(e){if(game.available()<=0)game.flag();else game.fail(String(e),config.failurePolicy);}
          await writeRecord(join(dir,'state.json'),snapshot(),false);
          options.onProgress?.({game:g.id,attempt:id,...snapshot()});
        }
      }catch(e){game.fail(String(e),config.failurePolicy);}
      finally{for(const e of engines)await e.close();}
      const attempt:Attempt={id,game:g.id,...snapshot(),outcome:game.outcome()??{kind:'invalid',reason:'attempt stopped',loser:game.side()},elapsedMs:monotonic.now()-started,progress};
      await finalize(directory,g,attempt);
      await writeRecord(join(directory,'summary.json'),await experimentStatus(directory),false);
      if(attempt.outcome.kind==='adjudication_required')break;
    }
    return await experimentStatus(directory);
  }finally{await release();}
}
export async function exportExperiment(directory:string):Promise<{pgn:string;sidecar:unknown}> {
  const {manifest,games}=await load(directory),done=await completed(directory,games);const pgn:string[]=[];
  for(const g of games) {
    const a=done.get(g.id);if(!a)continue;
    pgn.push(new FideRules(a.record).pgn(a.outcome.kind==='chess'?a.outcome:undefined,{White:manifest.identities[g.white]!.id.join('; '),Black:manifest.identities[g.black]!.id.join('; '),Round:g.id}));
  }return {pgn:pgn.join('\n\n'),sidecar:{manifest,scheduled:games,attempts:[...done.values()]}};
}
