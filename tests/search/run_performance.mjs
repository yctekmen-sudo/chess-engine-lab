// Serial canonical or diagnostic runs. Does not invoke m4_benchmark/M3.
import {execFileSync} from 'node:child_process';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {resolve,join,basename} from 'node:path';
import {cpus,platform,release} from 'node:os';
import {createHash} from 'node:crypto';
import {nativeExecutable,sharedLibrary} from '../../controller/test/native-paths.mjs';
const build=resolve(process.argv[2]??'build/native-release'),out=resolve(process.argv[3]??'work/search-performance/release');
const depths=(process.argv[4]??'4,5,6').split(',').map(Number),iterations=Number(process.argv[5]??3),warmup=Number(process.argv[6]??1);
const executable=join(build,basename(nativeExecutable('search_performance'))),module=sharedLibrary('material_evaluator',process.argv[7]?resolve(process.argv[7]):build);
const sha=p=>createHash('sha256').update(readFileSync(p)).digest('hex');
mkdirSync(out,{recursive:true});
const rows=[],expected=new Map();
for(const depth of depths)for(const mode of ['off','cold','warm']){
  const args=[module,String(depth),String(iterations),String(warmup),mode,'8388608'];
  const raw=execFileSync(executable,args,{encoding:'utf8',timeout:120000,maxBuffer:16*1024*1024});
  writeFileSync(join(out,`${depth}-${mode}.log`),raw);
  let row;
  for(const line of raw.split(/\r?\n/)){
    const fields=Object.fromEntries(line.split(' ').map(s=>{const i=s.indexOf('=');return [s.slice(0,i),s.slice(i+1)];}));
    if(line.startsWith('run=')){
      row={depth,mode,...Object.fromEntries(Object.entries(fields).map(([k,v])=>[k,['checksum','pv','best'].includes(k)?v:Number(v)])),counters:{}};
      if(expected.has(depth)&&expected.get(depth)!==row.checksum)throw Error(`Exact all-depth score/PV mismatch at ${depth}/${mode}`);
      expected.set(depth,row.checksum);rows.push(row);
    }else if(line.startsWith('counter='))row.counters[fields.counter]=Object.fromEntries(Object.entries(fields).filter(([k])=>k!=='counter').map(([k,v])=>[k,Number(v)]));
  }
  if(rows.filter(r=>r.depth===depth&&r.mode===mode).length!==iterations)throw Error('Missing benchmark results');
  console.log(JSON.stringify({build,depth,mode,ms:rows.filter(r=>r.depth===depth&&r.mode===mode).map(r=>r.ms)}));
}
for(const r of rows){const c=r.counters;if(!c.node)continue;
  const checks=[
    [c.node.calls,r.nodes,'node entries'],[c.evaluation.calls,r.evals,'evaluator calls'],
    [c.legal_generation.calls,c.status.calls+c.legal_moves.calls+c.require_legal.calls+c.decode_move.calls,'actual generator calls'],
    [c.require_legal.calls,c.encode_move.calls+c.make_move.calls,'required-legal attribution'],
    [c.tt_probe.calls,r.probes,'TT probes'],[c.tt_store.calls,r.stores,'TT stores']];
  for(const [a,b,label]of checks)if(a!==b)throw Error(`Counter invariant failed: ${label}: ${a} != ${b}`);
}
const result={revision:execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim(),platform:platform(),osRelease:release(),cpu:cpus()[0]?.model,node:process.version,
  build,executable,module,executableSha256:sha(executable),moduleSha256:sha(module),iterations,warmup,ttRequestedBytes:8388608,
  diagnosticTiming:process.env.CHESS_LAB_PERF_TIMING!==undefined,rows};
writeFileSync(join(out,'results.json'),JSON.stringify(result,null,2)+'\n');
const columns=['depth','mode','run','ms','cpu_ms','nodes','evals','cutoffs','probes','candidates','exact','bounds','bound_cutoffs','hints','rejects','stores','evictions','pv_researches','verification_nodes','verification_ms','retained','checksum','best','pv'];
const csv=v=>JSON.stringify(String(v));
writeFileSync(join(out,'results.csv'),[columns.join(','),...rows.map(r=>columns.map(k=>csv(r[k])).join(','))].join('\n')+'\n');
const counterColumns=['depth','mode','run','counter','calls','leaf','pre_probe','in_claim','in_ep','inclusive_ms','self_ms'];
writeFileSync(join(out,'counters.csv'),[counterColumns.join(','),...rows.flatMap(r=>Object.entries(r.counters).map(([counter,c])=>counterColumns.map(k=>csv(({...r,...c,counter})[k]??'')).join(',')))].join('\n')+'\n');