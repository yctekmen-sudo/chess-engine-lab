// Run before and after against the same Release preset and material artifact.
import {execFileSync} from 'node:child_process';
import {mkdirSync,writeFileSync} from 'node:fs';
import {resolve,join,basename} from 'node:path';
import {nativeExecutable,sharedLibrary} from '../../controller/test/native-paths.mjs';
const build=resolve(process.argv[2]??'build/native-release'),out=resolve(process.argv[3]??'work/m7.2/before');
mkdirSync(out,{recursive:true});
const executable=join(build,basename(nativeExecutable('m4_benchmark')));
const module=sharedLibrary('material_evaluator',build);
const cases={initial:'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',pawn:'4k3/8/8/8/8/8/P7/4K3 b - - 0 1',queen:'7k/8/5KQ1/8/8/8/8/8 w - - 0 1'};
const rows=[];
for(const [name,fen] of Object.entries(cases))for(const depth of [2,3,4]){
  const args=[module,String(depth),'3','1',fen,'','1','1',...(process.argv[4]==='legacy'?['0']:[])];
  const raw=execFileSync(executable,args,{encoding:'utf8',timeout:300000,maxBuffer:1024*1024});
  writeFileSync(join(out,`${name}-${depth}.txt`),raw);
  for(const line of raw.split('\n').filter(s=>/^order=1 run=\d+ status=/.test(s))){
    const fields=Object.fromEntries(line.trim().split(' ').map(x=>x.split('=')));
    rows.push({name,fen,depth,run:Number(fields.run),nodes:Number(fields.nodes),evaluations:Number(fields.evaluations),cutoffs:Number(fields.cutoffs),ms:Number(fields.ms),checksum:fields.checksum,decision:fields.decision,...(fields.score_kind?{scoreKind:fields.score_kind,scoreValue:fields.score_value,pvEnd:fields.pv_end,pv:fields.pv}:{})});
  }
}
writeFileSync(join(out,'results.json'),JSON.stringify({build,module,tt:'disabled',history:'incomplete, no replay',warmup:1,iterations:3,previousPV:true,rows},null,2));
console.log(JSON.stringify({out,measurements:rows.length}));
