#!/usr/bin/env node
// Independent MateTrack published-line verification. Does not prove uniqueness of mate.
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import crypto from 'node:crypto';
const args={};for(let i=2;i<process.argv.length;i+=2){if(!process.argv[i]?.startsWith('--')||!process.argv[i+1])throw Error('Expected --key value');args[process.argv[i].slice(2)]=process.argv[i+1]}
for(const k of ['repo','source','out'])if(!args[k])throw Error('Missing '+k);
const require=createRequire(path.resolve(args.repo,'controller/package.json'));
const {Chess}=require('chess.js');
const sha256=s=>crypto.createHash('sha256').update(s).digest('hex');
const source=fs.readFileSync(args.source,'utf8');
const expectedSha='79bdb4ac882aab713096b6b59a5bc07e9c3b4ba61c17f82d1ae606fd40fcdfb3';
if(sha256(source)!==expectedSha)throw Error('Selected MateTrack source SHA-256 mismatch');
const out=[],counts={},seen=new Set();
for(const line of source.split(/\r?\n/)){
 if(!line.trim())continue;
 const parts=line.match(/^(\S+)\s+([wb])\s+(\S+)\s+(\S+)\s+bm\s+#(\d+);\s+PV:\s+([^;]+);\s*$/);
 if(!parts)throw Error('Invalid MateTrack EPD line: '+line);
 const [_,board,turn,castle,ep,count,pvText]=parts;
 const mateIn=Number(count),fen=`${board} ${turn} ${castle} ${ep} 0 1`;
 const expectedPly=2*mateIn-1;
 const pv=pvText.trim().split(/\s+/);
 if(mateIn<1||mateIn>8||pv.length!==expectedPly)throw Error('Unexpected mate length/ply count');
 const chess=new Chess(fen,{skipValidation:false});
 if(chess.isGameOver())throw Error('MateTrack starting position already terminal');
 for(let j=0;j<pv.length;j++){
  let u=pv[j];if(!/^[a-h][1-8][a-h][1-8][qrbn]?$/.test(u))throw Error('Malformed published UCI: '+u);
  let m=chess.move({from:u.slice(0,2),to:u.slice(2,4),...(u.length===5?{promotion:u[4]}:{})});
  if(!m)throw Error('Illegal published PV '+fen+' '+u);
  if(j<pv.length-1&&chess.isGameOver())throw Error('Published PV terminates before final mate');
 }
 if(!chess.isCheckmate())throw Error('Published PV does not end at checkmate: '+fen);
 const id=`matetrack-m${mateIn}-${String((counts[mateIn]||0)+1).padStart(2,'0')}`;
 if(seen.has(fen))throw Error('Duplicate FEN: '+id);
 seen.add(fen);counts[mateIn]=(counts[mateIn]||0)+1;
 out.push({id,source:'MateTrack matetrackpv.epd',group:`mate_${mateIn}`,mateIn,fen,
   expectedMoves:[pv[0]],expectedType:'best',referencePV:pv,turn,
   sourceRepository:'vondele/matetrack',sourceCommit:'d280222d630df9c21d4c2f1e87dcb62ffc50e48b',
   sourceBlob:'1790ce63d7e9d74a2227b3a95d6b30d9ab2dbc90'});
}
if(out.length!==32||Array.from({length:8},(_,i)=>i+1).some(n=>counts[n]!==4))throw Error('Expected four positions for every mate-in-1..8');
const corpus={schema:2,kind:'forced_mate',baseline:'f92a170deee6d9e56b173101ad50aca6c619ab6d',
  selectedSourceSha256:expectedSha,counts,total:out.length,cases:out};
fs.writeFileSync(args.out,JSON.stringify(corpus,null,2)+'\n',{flag:'wx'});
console.log('MATETRACK_FREEZE_PASS '+JSON.stringify({total:out.length,counts,sha256:sha256(fs.readFileSync(args.out))}));
