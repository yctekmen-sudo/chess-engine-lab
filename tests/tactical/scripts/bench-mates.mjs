#!/usr/bin/env node
// Mate-specific cold UCI benchmark with explicit per-case timeout and PV checkmate audit.
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import readline from 'node:readline';
import crypto from 'node:crypto';
const args={};for(let i=2;i<process.argv.length;i+=2){if(!process.argv[i]?.startsWith('--')||!process.argv[i+1])throw Error('Expected --key value');args[process.argv[i].slice(2)]=process.argv[i+1]}
for(const k of ['engine','repo','corpus','out','platform','depth','repetitions','mate-from','mate-to','timeout-seconds'])if(!args[k])throw Error('Missing '+k);
const target=Number(args.depth),reps=Number(args.repetitions),mateFrom=Number(args['mate-from']),mateTo=Number(args['mate-to']),timeoutSeconds=Number(args['timeout-seconds']);
if(!Number.isInteger(target)||target<1||target>16||!Number.isInteger(reps)||reps<1||reps>5||!Number.isInteger(mateFrom)||!Number.isInteger(mateTo)||mateFrom<1||mateFrom>mateTo||mateTo>8||!Number.isInteger(timeoutSeconds)||timeoutSeconds<5||timeoutSeconds>3600)throw Error('Invalid experiment arguments');
const corpus=JSON.parse(fs.readFileSync(args.corpus,'utf8'));
if(corpus.schema!==2||corpus.kind!=='forced_mate'||corpus.total!==32||corpus.baseline!=='f92a170deee6d9e56b173101ad50aca6c619ab6d')throw Error('Corpus identity mismatch');
const cases=corpus.cases.filter(c=>c.mateIn>=mateFrom&&c.mateIn<=mateTo);
if(cases.length!==4*(mateTo-mateFrom+1))throw Error('Wrong mate cohort size');
const {Chess}=createRequire(path.resolve(args.repo,'controller/package.json'))('chess.js');
const isUci=x=>/^[a-h][1-8][a-h][1-8][qrbn]?$/.test(x);
class Engine{
 constructor(exe){
  this.p=(awaitImportSpawn())(exe,[],{stdio:['pipe','pipe','pipe']});this.lines=[];this.waiters=[];this.error=null;
  this.stderr='';this.p.stderr.setEncoding('utf8');this.p.stderr.on('data',d=>{this.stderr+=d;this.stderr=this.stderr.slice(-5000)});
  readline.createInterface({input:this.p.stdout}).on('line',line=>{this.lines.push(line);for(const w of this.waiters.slice())if(w.pred(line)){this.waiters.splice(this.waiters.indexOf(w),1);clearTimeout(w.timer);w.resolve(line)}});
  const fail=e=>{this.error=e instanceof Error?e:Error(String(e));for(const w of this.waiters.splice(0)){clearTimeout(w.timer);w.reject(this.error)}};
  this.p.on('error',fail);this.p.on('exit',(code,signal)=>fail(Error(`Engine exited ${code} ${signal}: ${this.stderr}`)));
 }
 send(s){if(this.error)throw this.error;this.p.stdin.write(s+'\n')}
 wait(pred,ms){if(this.error)return Promise.reject(this.error);return new Promise((resolve,reject)=>{let w={pred,resolve,reject,timer:null};w.timer=setTimeout(()=>{this.waiters=this.waiters.filter(x=>x!==w);reject(Error('Timeout waiting for UCI, tail='+this.lines.slice(-8).join(' | ')))},ms);this.waiters.push(w)})}
 async ready(){let p=this.wait(x=>x==='readyok',15000);this.send('isready');await p}
 async init(){let p=this.wait(x=>x==='uciok',15000);this.send('uci');await p;if(!this.lines.includes('option name NumericTT type check default true'))throw Error('Wrong evaluator default');await this.ready()}
 async search(fen,depth,cap){
  const at=this.lines.length;this.send('ucinewgame');this.send('setoption name Clear Hash');this.send('setoption name Hash value 16');this.send('setoption name NumericTT value true');await this.ready();
  this.send('position fen '+fen);const done=this.wait(x=>x.startsWith('bestmove '),cap+30000);let timedOut=false;
  const timer=setTimeout(()=>{timedOut=true;try{this.send('stop')}catch{}},cap);
  try{this.send('go depth '+depth);await done}finally{clearTimeout(timer)}
  return {lines:this.lines.slice(at),timedOut};
 }
 close(){try{this.send('quit')}catch{}this.p.kill()}
}
import {spawn} from 'node:child_process';
function awaitImportSpawn(){return spawn}
function pvCheck(fen,pv){
 const c=new Chess(fen);try{for(const u of pv){if(!isUci(u))return {legal:false,checkmate:false};const m=c.move({from:u.slice(0,2),to:u.slice(2,4),...(u.length===5?{promotion:u[4]}:{})});if(!m)return {legal:false,checkmate:false}}return {legal:true,checkmate:c.isCheckmate(),draw:c.isDraw(),endTurn:c.turn()}}catch{return {legal:false,checkmate:false}}
}
function parseIteration(line,fixture){
 const m=/^info depth (\d+) time (\d+) nodes (\d+) multipv (\d+)(?: score (?:cp (-?\d+)|mate (-?\d+)))? pv(?: (.*))?$/.exec(line);
 if(!m)return null;
 const pv=(m[7]||'').split(/\s+/).filter(Boolean);const audit=pvCheck(fixture.fen,pv);
 return {depth:Number(m[1]),elapsedMs:Number(m[2]),nodes:Number(m[3]),multipv:Number(m[4]),mateMoves:m[6]===undefined?null:Number(m[6]),centipawns:m[5]===undefined?null:Number(m[5]),pv,firstMove:pv[0]||null,referenceFirstMove:pv[0]===fixture.expectedMoves[0],pvLegal:audit.legal,pvCheckmate:audit.checkmate,correctMateDistance:Number(m[6])===fixture.mateIn};
}
function parseSearch(fixture,result,target){
 const {lines,timedOut}=result;
 const errors=lines.filter(s=>s.includes('chesslab rejected')||s.includes('chesslab fatal'));if(errors.length)throw Error('UCI rejected mate fixture: '+errors.join(' | '));
 const best=lines.filter(s=>s.startsWith('bestmove '));if(best.length!==1)throw Error('No unique bestmove emitted');
 const finals=lines.filter(s=>s.startsWith('info string chesslab final_score '));
 const match=finals.length===1?/^info string chesslab final_score v[12] search (\d+) depth (\d+) status (completed|cancelled) bestmove (\S+) score (finite|white_mate|black_mate) (\S+)$/.exec(finals[0]):null;
 const fallbacks=lines.filter(s=>s.includes('chesslab fallback unevaluated'));
 if(!match&&!fallbacks.length)throw Error('Missing final score without fallback');
 const depthRows=lines.map(s=>parseIteration(s,fixture)).filter(Boolean);
 if(depthRows.some((x,i)=>x.depth!==i+1||x.multipv!==1))throw Error('Nonconsecutive completed depths');
 if(depthRows.length>target)throw Error('Depth exceeds requested depth');
 if(match){if(Number(match[2])!==depthRows.length||best[0]!=='bestmove '+match[4])throw Error('Structured final score inconsistent with iterations');if(depthRows.at(-1)?.firstMove!==match[4])throw Error('Final PV differs from bestmove');}
 if(!timedOut&&depthRows.length!==target)throw Error('Depth missing without timeout');
 if(!timedOut&&match?.[3]!=='completed')throw Error('Uncancelled search has cancelled status');
 return {scoreContract:match?finals[0].split(' ')[4]:null,timedOut,completedAllDepths:depthRows.length===target,finalStatus:match?.[3]??'unevaluated_fallback',bestmove:best[0].split(' ')[1],scoreKind:match?.[5]??null,scoreValue:match?.[6]??null,depths:depthRows};
}
async function main(){
 const out=fs.createWriteStream(args.out,{flags:'wx'});let engine=new Engine(args.engine),count=0;
 try{
  await engine.init();for(let rep=0;rep<reps;rep++)for(const fixture of cases){
   const t=process.hrtime.bigint();const res=await engine.search(fixture.fen,target,timeoutSeconds*1000);
   const data=parseSearch(fixture,res,target);
   const record={platform:args.platform,rep,id:fixture.id,group:fixture.group,mateIn:fixture.mateIn,expectedMove:fixture.expectedMoves[0],wallMs:Number(process.hrtime.bigint()-t)/1e6,...data};
   out.write(JSON.stringify(record)+'\n');count++;
   console.log(`${args.platform}: ${count}/${cases.length*reps} ${fixture.id}: ${data.completedAllDepths?'completed':'partial'} d${data.depths.length} mate=${data.depths.at(-1)?.mateMoves??'-'} pvMate=${data.depths.at(-1)?.pvCheckmate??false}`);
  }
 }finally{engine.close();out.end();await new Promise(resolve=>out.on('finish',resolve))}
 console.log('MATE_BENCH_FINISHED '+JSON.stringify({records:count,corpusSha256:crypto.createHash('sha256').update(fs.readFileSync(args.corpus)).digest('hex')}));
}
main().catch(e=>{console.error('MATE_BENCH_FAILED:',e.stack||String(e));process.exitCode=1});
