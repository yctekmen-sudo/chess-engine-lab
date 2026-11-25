#!/usr/bin/env node
// Deterministic, cold-UCI, fixed-depth per-position tactical reference measurements.
import fs from 'node:fs';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import readline from 'node:readline';
import {verdict, isUci} from './core.mjs';
function args(){const a={};for(let i=2;i<process.argv.length;i+=2){if(!process.argv[i]?.startsWith('--')||!process.argv[i+1])throw Error('Expected --key value arguments');a[process.argv[i].slice(2)]=process.argv[i+1];}return a;}
class Engine {
 constructor(binary){
   this.p=spawn(binary,[],{stdio:['pipe','pipe','pipe']});this.lines=[];this.waiters=[];this.failure=null;
   this.stderr='';this.p.stderr.setEncoding('utf8');this.p.stderr.on('data',x=>this.stderr+=x);
   readline.createInterface({input:this.p.stdout}).on('line',line=>{
     this.lines.push(line);for(const w of this.waiters.slice())if(w.predicate(line)){
       this.waiters.splice(this.waiters.indexOf(w),1);clearTimeout(w.timer);w.resolve(line);
     }
   });
   const fail=(e)=>{this.failure=e instanceof Error?e:Error(String(e));for(const w of this.waiters.splice(0)){clearTimeout(w.timer);w.reject(this.failure)}};
   this.p.on('error',fail);this.p.on('exit',(code,signal)=>fail(Error('UCI exited '+code+' '+signal+' '+this.stderr.slice(-2000))));
 }
 send(s){if(this.failure)throw this.failure;this.p.stdin.write(s+'\n');}
 wait(predicate,ms=120000){if(this.failure)return Promise.reject(this.failure);return new Promise((resolve,reject)=>{
   const w={predicate,resolve,reject,timer:null};w.timer=setTimeout(()=>{this.waiters=this.waiters.filter(v=>v!==w);reject(Error('Timed out waiting for UCI; tail: '+this.lines.slice(-8).join(' | ')));},ms);this.waiters.push(w);
 });}
 async ready(){const ready=this.wait(x=>x==='readyok',15000);this.send('isready');await ready;}
 async init(){const ok=this.wait(x=>x==='uciok',15000);this.send('uci');await ok;
   if(!this.lines.includes('option name NumericTT type check default true'))throw Error('Engine does not advertise packaged material numeric TT default');await this.ready();}
 async search(fen,depth,timeout){const at=this.lines.length;this.send('ucinewgame');this.send('setoption name Clear Hash');this.send('setoption name Hash value 16');
   this.send('setoption name NumericTT value true');await this.ready();this.send('position fen '+fen);
   const got=this.wait(x=>x.startsWith('bestmove '),timeout);this.send('go depth '+depth);await got;
   return this.lines.slice(at);
 }
 close(){try{this.send('quit')}catch{}this.p.kill();}
}
function parseIteration(line){
 const m=/^info depth (\d+) time (\d+) nodes (\d+) multipv (\d+)(?: score (?:cp (-?\d+)|mate (-?\d+)))? pv(?: (.*))?$/.exec(line);
 if(!m)return null;
 return {depth:Number(m[1]),elapsedMs:Number(m[2]),nodes:Number(m[3]),multipv:Number(m[4]),
   centipawns:m[5]===undefined?null:Number(m[5]),mateMoves:m[6]===undefined?null:Number(m[6]),
   pv:(m[7]||'').split(/\s+/).filter(Boolean)};
}
function parseSearch(lines,fixture,depth){
 const errors=lines.filter(s=>s.includes('chesslab rejected')||s.includes('chesslab fatal'));
 if(errors.length)throw Error('UCI rejected search: '+errors.join(' | '));
 const final=lines.filter(s=>s.startsWith('info string chesslab final_score '));
 if(final.length!==1)throw Error(`Expected 1 structured final score, got ${final.length}`);
 const match=/^info string chesslab final_score v[12] search (\d+) depth (\d+) status (\S+) bestmove (\S+) score (finite|white_mate|black_mate) (\S+)$/.exec(final[0]);
 if(!match||match[3]!=='completed'||Number(match[2])!==depth)throw Error('Incomplete or malformed final score: '+final[0]);
 const best=lines.filter(s=>s.startsWith('bestmove '));if(best.length!==1||best[0]!=='bestmove '+match[4])throw Error('Bestmove mismatch');
 if(!isUci(match[4]))throw Error('Unexpected illegal-looking UCI move '+match[4]);
 const met=lines.filter(s=>s.startsWith('info string chesslab metrics '));
 const mm=/^info string chesslab metrics evaluator_calls (\d+) tt_lookups (\d+) tt_exact_hits (\d+)$/.exec(met[0]||'');if(met.length!==1||!mm)throw Error('Metrics missing');
 const depths=lines.map(parseIteration).filter(Boolean);
 if(depths.length!==depth||depths.some((x,i)=>x.depth!==i+1||x.multipv!==1))throw Error('Unexpected iterative depth sequence: '+depths.map(x=>x.depth));
 if(depths[depths.length-1].pv[0]!==match[4])throw Error('Final PV does not start with structured bestmove');
 if(depths.some(x=>x.pv.some(m=>!isUci(m))))throw Error('Malformed UCI PV');
 const result=depths.map(x=>({...x,firstMove:x.pv[0]||null,hit:verdict(fixture,x.pv[0]||'')}));
 const full=Object.assign({}, {scoreContract:final[0].split(' ')[4],bestmove:match[4],scoreKind:match[5],scoreValue:match[6],
   evalCalls:Number(mm[1]),ttLookups:Number(mm[2]),ttExactHits:Number(mm[3]),depths:result});
 return full;
}
async function main(){
 const a=args();for(const k of ['engine','corpus','out','platform','depth','repetitions'])if(!a[k])throw Error('Missing --'+k);
 const depth=Number(a.depth),reps=Number(a.repetitions),limit=Number(a.limit??0),timeout=Number(a.timeout??120000);
 if(!Number.isInteger(depth)||depth<1||depth>12||!Number.isInteger(reps)||reps<1||reps>5)throw Error('Invalid depth/reps');
 const corpus=JSON.parse(fs.readFileSync(a.corpus,'utf8'));
 if(corpus.schema!==1||corpus.baseline!=='f92a170deee6d9e56b173101ad50aca6c619ab6d'||corpus.total!==204)throw Error('Unexpected corpus/baseline');
 let fixtures=corpus.cases;if(limit){if(!Number.isInteger(limit)||limit<1||limit>fixtures.length)throw Error('Invalid limit');fixtures=fixtures.slice(0,limit)}
 const out=fs.createWriteStream(a.out,{flags:'wx'});const engine=new Engine(a.engine);
 let done=0,successful=false;
 try{await engine.init();for(let rep=0;rep<reps;rep++)for(const f of fixtures){
   const start=process.hrtime.bigint(),lines=await engine.search(f.fen,depth,timeout);
   const result=parseSearch(lines,f,depth),wallMs=Number(process.hrtime.bigint()-start)/1e6;
   out.write(JSON.stringify({platform:a.platform,rep,id:f.id,group:f.group,expectedType:f.expectedType,
     expectedMoves:f.expectedMoves,wallMs,...result})+'\n');done++;
   if(done%10===0)console.log(`${a.platform}: ${done}/${fixtures.length*reps} completed; recent ${f.id} d${depth} ${result.bestmove} hit=${verdict(f,result.bestmove)}`);
 }successful=true;}finally{engine.close();out.end();await new Promise(resolve=>out.on('finish',resolve));}
 if(!successful)throw Error('Search incomplete');
 console.log(JSON.stringify({platform:a.platform,records:done,depth,repetitions:reps,corpusSha256:crypto.createHash('sha256').update(fs.readFileSync(a.corpus)).digest('hex')}));
}
main().catch(e=>{console.error('TACTICAL_BENCH_FAILED:',e.stack||e);process.exitCode=1;});
