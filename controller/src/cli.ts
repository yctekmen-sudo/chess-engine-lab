#!/usr/bin/env node
import { readFile, writeFile } from 'node:fs/promises';
import { analyze } from './analysis.js';
import { createExperiment, experimentStatus, exportExperiment, resumeExperiment, validateConfig } from './experiment.js';
async function json(path:string) {return JSON.parse(await readFile(path,'utf8')) as unknown;}
async function main(args:string[]):Promise<unknown> {
  const [command,action,arg,extra]=args;
  if(command==='analyze'&&action) {
    const c=validateConfig(await json(action));
    if(c.limit.kind==='clock')throw Error('clock mode is for live matches; analysis requires depth or movetime');
    return analyze(c.engines[0],c.openings[0]!,c.limit,{searchCapMs:c.searchCapMs});
  }
  if(command==='match'&&action&&arg){await createExperiment(arg,await json(action));return resumeExperiment(arg);}
  if(command==='experiment'&&action&&arg) {
    if(action==='status')return experimentStatus(arg);
    if(action==='resume')return resumeExperiment(arg,{takeoverStaleLock:extra==='--takeover-stale-lock'});
    if(action==='export') {
      const out=await exportExperiment(arg);if(extra){await writeFile(extra+'.pgn',out.pgn);await writeFile(extra+'.json',JSON.stringify(out.sidecar,null,2));return {pgn:extra+'.pgn',sidecar:extra+'.json'};}return out;
    }
  }
  throw Error('Usage: chess-lab analyze CONFIG | match CONFIG DIRECTORY | experiment status|resume|export DIRECTORY [--takeover-stale-lock|EXPORT_PREFIX]');
}
main(process.argv.slice(2)).then(value=>console.log(JSON.stringify(value,null,2)),error=>{console.error(JSON.stringify({error:String(error)}));process.exitCode=1;});
