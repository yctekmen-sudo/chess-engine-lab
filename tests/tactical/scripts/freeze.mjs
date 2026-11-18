#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import crypto from 'node:crypto';
import {parseEpd, parseLichess, parseCsv, uci, isUci} from './core.mjs';
function args() {const a={};for(let i=2;i<process.argv.length;i+=2){if(!process.argv[i]?.startsWith('--')||!process.argv[i+1])throw Error('Expected --name value pairs');a[process.argv[i].slice(2)]=process.argv[i+1];}return a;}
function main(){
 const a=args();for(const x of ['repo','epd','csv','out'])if(!a[x])throw Error('Missing --'+x);
 const require=createRequire(path.resolve(a.repo,'controller','package.json'));
 const {Chess}=require('chess.js');
 const epd=fs.readFileSync(a.epd,'utf8');const examples=fs.readFileSync(a.csv,'utf8');
 const sha=s=>crypto.createHash('sha256').update(s).digest('hex');
 const out=[];const seenIds=new Set(),fenSet=new Set();
 function add(r){
   if(seenIds.has(r.id))throw Error('Duplicate case ID: '+r.id);
   if(fenSet.has(r.fen))throw Error('Duplicate resulting FEN: '+r.id);
   if(!r.expectedMoves?.length||r.expectedMoves.some(m=>!isUci(m)))throw Error('Malformed expected UCI moves '+r.id);
   const c=new Chess(r.fen);
   if(c.isGameOver())throw Error('Terminal puzzle cannot be searched: '+r.id);
   seenIds.add(r.id);fenSet.add(r.fen);out.push(r);
 }
 for(const raw of epd.split(/\r?\n/)){
   const x=parseEpd(raw);if(!x)continue;const chess=new Chess(x.fen);
   const moves=x.rawMoves.map(s=>{let m;try{m=chess.move(s,{strict:false});}catch(e){throw Error(`Illegal EPD SAN ${x.id} ${s}: ${e.message}`);}if(!m)throw Error(`Illegal EPD SAN: ${x.id} ${s}`);chess.undo();return uci(m)});
   add({id:'arasan:'+x.id, source:'Arasan 2026', group:x.expectedType==='best'?'arasan_best':'arasan_avoid',
     fen:x.fen,expectedType:x.expectedType,expectedMoves:moves,comment:x.comment});
 }
 if(out.filter(r=>r.source==='Arasan 2026').length!==200)throw Error('Expected exactly 200 Arasan cases');
 for(const row of parseCsv(examples)){
   const x=parseLichess(row);const chess=new Chess(x.fen);
   function play(s){const m=chess.move({from:s.slice(0,2),to:s.slice(2,4),...(s.length===5?{promotion:s[4]}:{})});if(!m)throw Error('Illegal puzzle move '+x.id+': '+s);return m;}
   play(x.setup);const fen=chess.fen();
   for(const m of x.solution)play(m);
   add({id:'lichess:'+x.id,source:'Lichess CC0 samples',group:'lichess_smoke',fen,
     expectedType:'best',expectedMoves:[x.solution[0]],solutionLine:x.solution,
     setupMove:x.setup,rating:x.rating,themes:x.themes});
 }
 const corpus={schema:1,baseline:'f92a170deee6d9e56b173101ad50aca6c619ab6d',
   sourceBlobs:{arasan:'29fa318d960c73dc511eb4589c32ae9595f4b9ba',lichessSampleSha256:sha(examples)},
   total:out.length,counts:Object.fromEntries([...new Set(out.map(x=>x.group))].map(k=>[k,out.filter(x=>x.group===k).length])),cases:out};
 fs.writeFileSync(a.out,JSON.stringify(corpus,null,2)+'\n');console.log(JSON.stringify({cases:corpus.total,groups:corpus.counts,sha256:sha(fs.readFileSync(a.out))}));
}
try{main()}catch(e){console.error('CORPUS_FREEZE_FAILED:',e.stack||String(e));process.exitCode=1;}
