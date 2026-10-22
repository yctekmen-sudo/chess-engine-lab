import {readFileSync,writeFileSync} from 'node:fs';
const before=JSON.parse(readFileSync(process.argv[2]??'work/m7.2/before/results.json'));
const after=JSON.parse(readFileSync(process.argv[3]??'work/m7.2/after/results.json'));
const median=rows=>rows.map(r=>r.ms).sort((a,b)=>a-b)[1];
const table=['| Position | Depth | Nodes before/after | Evaluations before/after | Cutoffs before/after | Median ms before/after | Speedup |','| --- | --- | --- | --- | --- | --- | --- |'];
const csv=['position,depth,run,before_nodes,after_nodes,before_evaluations,after_evaluations,before_cutoffs,after_cutoffs,before_ms,after_ms,checksum,decision,score_kind,score_value,pv_end,pv'];
for(let i=0;i<before.rows.length;i++){
 const a=before.rows[i],b=after.rows[i];
 if(a.name!==b.name||a.depth!==b.depth||a.run!==b.run||a.checksum!==b.checksum)throw Error('exact-result checksum mismatch');
 csv.push([a.name,a.depth,a.run,a.nodes,b.nodes,a.evaluations,b.evaluations,a.cutoffs,b.cutoffs,a.ms,b.ms,a.checksum,b.decision,b.scoreKind,b.scoreValue,b.pvEnd,JSON.stringify((b.pv??'').trim().replace(/,$/,''))].join(','));
 if(i%3===0){const x=median(before.rows.slice(i,i+3)),y=median(after.rows.slice(i,i+3));table.push(`| ${a.name} | ${a.depth} | ${a.nodes}/${b.nodes} | ${a.evaluations}/${b.evaluations} | ${a.cutoffs}/${b.cutoffs} | ${x.toFixed(3)}/${y.toFixed(3)} | ${(x/y).toFixed(2)}x |`);}
}
writeFileSync('docs/m7.2-benchmark-results.csv',csv.join('\n')+'\n');
console.log(table.join('\n'));
