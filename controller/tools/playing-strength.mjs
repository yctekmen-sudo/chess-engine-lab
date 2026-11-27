// Local serial tournament coordinator. Game execution and durability belong to controller.
import { readFile, readdir, mkdir, writeFile, stat } from 'node:fs/promises';
import { join, resolve, isAbsolute, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
const here=dirname(fileURLToPath(import.meta.url)), root=resolve(here,'../..');
const json=async p=>JSON.parse((await readFile(p,'utf8')).replace(/^\uFEFF/,''));
const hash=async p=>createHash('sha256').update(await readFile(p)).digest('hex');
const ensure=(ok,msg)=>{if(!ok)throw Error(msg);};
const safeId=s=>typeof s==='string'&&/^[a-zA-Z][a-zA-Z0-9_-]{0,47}$/.test(s)&&!/^(con|prn|aux|nul|com[1-9]|lpt[1-9])$/i.test(s);
export function summarize(scheduled,attempts){
  const ids=new Set(), byGame=new Map();
  for(const g of scheduled){ensure(!ids.has(g.id)&&[0,1].includes(g.white)&&g.black===1-g.white,'bad schedule');ids.add(g.id);}
  for(const a of attempts){ensure(ids.has(a.game)&&!byGame.has(a.game),'duplicate/foreign official game');byGame.set(a.game,a);}
  let wins=0,draws=0,losses=0,pending=0,unresolved=0,failures=0;const pairs=new Map();
  for(const g of scheduled){const a=byGame.get(g.id);let score=null;
    if(!a)pending++;
    else if(a.outcome.kind==='chess'){
      const r=a.outcome.result;ensure(['1-0','0-1','1/2-1/2'].includes(r),'invalid chess result');
      score=r==='1/2-1/2'?.5:(r==='1-0'?g.white:g.black)===0?1:0;
      if(score===1)wins++;else if(score===.5)draws++;else losses++;
    }else if(a.outcome.kind==='invalid'&&a.outcome.reason==='explicit maxPlies work limit')unresolved++;
    else if(a.outcome.kind==='adjudication_required')unresolved++;
    else failures++;
    if(!pairs.has(g.pair))pairs.set(g.pair,[]);pairs.get(g.pair).push({white:g.white,score});
  }
  const pairScores=[];for(const p of pairs.values()){ensure(p.length===2&&p[0].white!==p[1].white,'unbalanced pair');if(p.every(g=>g.score!==null))pairScores.push((p[0].score+p[1].score)/2);}
  const n=scheduled.length, points=wins+.5*draws, resolved=wins+draws+losses;
  const elo=p=>p>0&&p<1?400*Math.log10(p/(1-p)):null;
  // Approximate opening-cluster interval, only if every scheduled pair is resolved.
  // Student-t conservative small-sample critical values; no finite Elo at endpoints.
  let interval=null;
  if(pairScores.length===pairs.size&&pairScores.length>=2){const k=pairScores.length,m=points/n;const variance=pairScores.reduce((s,x)=>s+(x-m)**2,0)/(k-1);const t=[0,12.706,4.303,3.182,2.776,2.571,2.447,2.365,2.306,2.262,2.228,2.201,2.179,2.16,2.145,2.131,2.12,2.11,2.101,2.093,2.086,2.08,2.074,2.069,2.064,2.06,2.056,2.052,2.048,2.045,2.042][Math.min(k-1,30)];if(variance>0){const d=t*Math.sqrt(variance/k),lo=Math.max(0,m-d),hi=Math.min(1,m+d);interval={method:'approximate opening-pair t interval; openings not necessarily random',score:[lo,hi],elo:[elo(lo),elo(hi)]};}}
  return {scheduled:n,completed:attempts.length,wins,draws,losses,unresolved,failures,pending,resolvedScore:resolved?points/resolved:null,scoreBounds:n?[points/n,(points+n-resolved)/n]:null,completeResolvedPairs:pairScores.length,openingPairs:pairs.size,eloEstimate:resolved===n&&n?elo(points/n):null,interval};
}
export function validateInput(raw){
  ensure(raw&&typeof raw==='object'&&!Array.isArray(raw),'expected tournament config');
  ensure(raw.reference&&Array.isArray(raw.challengers)&&raw.challengers.length>0,'reference and challengers required');
  const arms=[raw.reference,...raw.challengers],ids=new Set();
  for(const a of arms){ensure(safeId(a.id)&&!ids.has(a.id.toLowerCase()),'unsafe or duplicate arm id');ids.add(a.id.toLowerCase());ensure(a.engine&&isAbsolute(a.engine.path),'absolute engine path required');ensure(typeof a.sourceRevision==='string'&&a.sourceRevision.length>0,'sourceRevision declaration required');ensure(a.artifacts===undefined||Array.isArray(a.artifacts)&&a.artifacts.every(p=>typeof p==='string'&&isAbsolute(p)),'absolute artifact paths required');}
  ensure(raw.openings===undefined||Array.isArray(raw.openings)&&raw.openings.length>0,'invalid openings');
  ensure(!raw.match?.engines&&!raw.match?.openings&&!raw.match?.engineLimits,'match may not override engines, openings or equal limits');
  ensure(raw.match?.limit&&['depth','movetime'].includes(raw.match.limit.kind),'equal depth or movetime required');
  ensure(!raw.match.time||raw.match.time.kind==='untimed','untimed game required');
  ensure(raw.match.failurePolicy===undefined||raw.match.failurePolicy==='invalid','failures must not score points');
  return raw;
}
async function inventory(dir){const out={};async function walk(d){for(const ent of (await readdir(d,{withFileTypes:true})).sort((a,b)=>a.name.localeCompare(b.name))){const p=join(d,ent.name);if(ent.isDirectory())await walk(p);else if(ent.isFile())out[p.slice(dir.length+1).replaceAll('\\','/')]=await hash(p);else throw Error('unexpected artifact '+p);}}await walk(dir);return out;}
async function capturePlan(raw,c){
  validateInput(raw);const source=raw.openings??await json(join(here,'playing-strength-openings.json'));
  const openings=source.map(o=>{ensure(o&&Array.isArray(o.moves)&&o.moves.every(m=>typeof m==='string'),'opening moves required');ensure(o.initialFen===undefined||o.initialFen===c.DEFAULT_POSITION||typeof o.complete==='boolean','custom FEN requires explicit history completeness');return {initialFen:o.initialFen??c.DEFAULT_POSITION,moves:o.moves,complete:o.complete??true};});
  const keys=new Set();for(const o of openings){const r=new c.FideRules(o);ensure(!r.terminal(),'terminal opening');const key=c.canonical(r.record());ensure(!keys.has(key),'duplicate opening');keys.add(key);}
  const arms=[];for(const a of [raw.reference,...raw.challengers]){
    const sha256=await hash(a.engine.path);ensure(!a.engine.sha256||a.engine.sha256===sha256,'declared executable hash mismatch '+a.id);
    const engine={...a.engine,sha256};const files=new Set([engine.path,...(a.artifacts??[])]);
    for(const p of [...(engine.args??[]),engine.options?.Evaluator??''])if(isAbsolute(p)){const s=await stat(p);if(s.isFile())files.add(p);else throw Error('non-file artifact '+p);}
    const artifacts=[];for(const p of [...files].sort())artifacts.push({path:p,sha256:await hash(p)});
    arms.push({id:a.id,sourceRevision:a.sourceRevision,engine,artifacts});
  }
  const matches=arms.slice(1).map(a=>({id:a.id,config:c.validateConfig({...raw.match,engines:[a.engine,arms[0].engine],openings,time:{kind:'untimed'},failurePolicy:'invalid'})}));
  const controllerRevision=execFileSync('git',['-C',root,'rev-parse','HEAD'],{encoding:'utf8'}).trim();
  return {schema:1,arms,matches,controllerRevision,driverSHA256:await hash(fileURLToPath(import.meta.url)),controllerDist:await inventory(join(root,'controller/dist')),controllerLock:await hash(join(root,'controller/package-lock.json')),node:process.version,platform:process.platform,arch:process.arch,env:Object.fromEntries(Object.entries(process.env).filter(([k])=>/^CELAB_/i.test(k)).sort()),execution:'serial by challenger; controller authoritative game rules; no evaluation adjudication'};
}
async function report(c,dir,plan,output){const matches=Object.create(null);for(const m of plan.matches){const d=join(dir,'matches',m.id);if(!await c.exists(join(d,'manifest.json'))){ensure(!await c.exists(join(d,'schedule.json')),'partial match initialization: '+m.id);matches[m.id]=summarize(c.schedule(m.config),[]);continue;}
  const x=await c.exportExperiment(d),s=x.sidecar;ensure(c.canonical(s.manifest.config)===c.canonical(m.config),'match config drift');matches[m.id]={...summarize(s.scheduled,s.attempts),searchSummary:s.searchSummary};
  if(output){await writeFile(join(output,m.id+'.pgn'),x.pgn+'\n',{flag:'wx'});await writeFile(join(output,m.id+'.json'),JSON.stringify(s,null,2)+'\n',{flag:'wx'});}}
  return {reference:plan.arms[0].id,matches};}
async function main(args){
  ensure(/^v24\./.test(process.version),'Node 24 required');const c=await import('../dist/index.js');const [cmd,...a]=args;
  if(cmd==='run'){
    const [config,dirArg,...flags]=a;ensure(config&&dirArg,'run CONFIG DIRECTORY [--max-games N] [--minutes N] [--takeover-stale-lock]');let max=Infinity,minutes=Infinity,takeover=false;
    for(let i=0;i<flags.length;i++){const f=flags[i];if(f==='--takeover-stale-lock'){takeover=true;continue;}ensure(['--max-games','--minutes'].includes(f),'unknown flag '+f);const v=Number(flags[++i]);ensure(Number.isSafeInteger(v)&&v>0,'positive integer required');if(f==='--max-games')max=v;else minutes=v;}
    const dir=resolve(dirArg),release=await c.writerLock(dir,takeover);try{
      const plan=await capturePlan(await json(config),c),p=join(dir,'tournament.json');if(await c.exists(p))ensure(c.canonical(await c.readRecord(p))===c.canonical(plan),'tournament inputs/artifacts/environment changed');else await c.writeRecord(p,plan);
      let remaining=max;const until=Date.now()+minutes*60000;
      for(const m of plan.matches){if(remaining<=0||Date.now()>=until)break;const d=join(dir,'matches',m.id);if(!await c.exists(join(d,'manifest.json'))){ensure(!await c.exists(join(d,'schedule.json')),'partial initialization; inspect before recovery');await c.createExperiment(d,m.config);}
        const manifest=await c.readRecord(join(d,'manifest.json'));ensure(c.canonical(manifest.config)===c.canonical(m.config),'match config drift');
        const before=await c.experimentStatus(d);if(before.completed===before.scheduled){const x=await c.exportExperiment(d);if(summarize(x.sidecar.scheduled,x.sidecar.attempts).failures)break;continue;}
        let launched=0,last='';const result=await c.resumeExperiment(d,{takeoverStaleLock:takeover,shouldContinue:()=>{if(launched>=remaining||Date.now()>=until)return false;launched++;return true;},onProgress:x=>{if(last!==x.game){last=x.game;console.error(`${m.id}: game ${before.completed+launched}/${before.scheduled} (${x.game})`);}}});remaining-=result.completed-before.completed;console.error(`${m.id}: ${result.completed}/${result.scheduled} complete`);
        const done=await c.exportExperiment(d);const score=summarize(done.sidecar.scheduled,done.sidecar.attempts);if(score.failures||result.counts.unresolved>0||result.counts.technicalForfeits>0)break;
      }
      const result=await report(c,dir,plan);await c.writeRecord(join(dir,'tournament-summary.json'),result,false);console.log(JSON.stringify(result,null,2));
      if(Object.values(result.matches).some(m=>m.failures))process.exitCode=2;
    }finally{await release();}
  }else if(cmd==='status'||cmd==='export'){
    ensure(a.length===(cmd==='status'?1:2),'status DIRECTORY | export DIRECTORY OUTPUT');const dir=resolve(a[0]),release=await c.writerLock(dir);try{const plan=await c.readRecord(join(dir,'tournament.json'));let output;if(cmd==='export'){output=resolve(a[1]);await mkdir(output,{recursive:false});}const result=await report(c,dir,plan,output);if(output)await writeFile(join(output,'summary.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(result,null,2));}finally{await release();}
  }else throw Error('run CONFIG DIRECTORY | status DIRECTORY | export DIRECTORY OUTPUT');
}
if(process.argv[1]&&pathToFileURL(resolve(process.argv[1])).href===import.meta.url)main(process.argv.slice(2)).catch(e=>{console.error(e.stack??String(e));process.exitCode=1;});
