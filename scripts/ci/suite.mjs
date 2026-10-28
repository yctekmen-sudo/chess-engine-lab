// Local CI execution and evidence only. No source/engine/test policy changes.
import {spawn,spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {readFileSync,writeFileSync,appendFileSync,mkdirSync,existsSync,renameSync,lstatSync,readlinkSync} from 'node:fs';
import {resolve,join,dirname,basename} from 'node:path';
import {platform,release,cpus} from 'node:os';
import {fileURLToPath} from 'node:url';

const here=dirname(fileURLToPath(import.meta.url));
const sha=s=>createHash('sha256').update(s).digest('hex');
// Reviewed coverage floors, not discovered counts. Additions are allowed;
// reducing a floor requires an explicit source/documentation review.
export const testBaselines=Object.freeze({
  'controller-test':107,'gui-test':14,'playwright-e2e':5,
  'windows-desktop-smoke':2,'harness-tests':54,
});
// Historical workflow definitions are optional evidence, never execution inputs.
// Only absence is expected; other read failures must still block the worker.
export function workflowEvidence(repo){
  return Object.fromEntries(['native','wasm','controller','gui'].map(name=>{
    const path=`.github/workflows/${name}.yml`;
    try{return [name,{path,status:'present',sha256:sha(readFileSync(join(repo,path)))}];}
    catch(error){if(error.code!=='ENOENT')throw error;return [name,{path,status:'absent'}];}
  }));
}
const git=(repo,args)=>{
  const r=spawnSync('git',['-C',repo,...args],{encoding:'utf8',maxBuffer:32*1024*1024,env:{...process.env,GIT_OPTIONAL_LOCKS:'0'}});
  if(r.error||r.status!==0)throw Error(`git ${args.join(' ')}: ${r.error?.message??r.stderr}`);
  return r.stdout;
};
export function snapshot(repo){
  repo=resolve(repo);
  const tracked=git(repo,['ls-files','-z']).split('\0').filter(Boolean).sort();
  // -v exposes assume-unchanged (lowercase tags) and skip-worktree (S/s).
  // Do not clear these flags or trust diff's omission of their working bytes.
  const taggedIndex=git(repo,['ls-files','-v','-z']);
  const indexFlags=taggedIndex.split('\0').filter(Boolean).flatMap(entry=>{
    const tag=entry[0],assumeUnchanged=tag!==tag.toUpperCase(),skipWorktree=tag.toUpperCase()==='S';
    return assumeUnchanged||skipWorktree?[{path:entry.slice(2),assumeUnchanged,skipWorktree}]:[];
  });
  const untracked=git(repo,['ls-files','--others','--exclude-standard','-z']).split('\0').filter(Boolean).sort();
  const dirty=[...new Set([...git(repo,['diff','--cached','HEAD','--name-only','-z']).split('\0'),...git(repo,['diff','--name-only','-z']).split('\0')].filter(Boolean))].sort();
  const digest=paths=>sha(paths.map(p=>{
    const path=join(repo,p);let content;
    try{content=sha(lstatSync(path).isSymbolicLink()?readlinkSync(path):readFileSync(path));}
    catch(e){if(e.code!=='ENOENT')throw e;content='MISSING';}
    return p+'\0'+content;
  }).join('\0'));
  return {repo,commit:git(repo,['rev-parse','HEAD']).trim(),branch:git(repo,['branch','--show-current']).trim(),
    dirtyTracked:dirty,untracked,indexFlags,indexFingerprint:sha(git(repo,['ls-files','--stage','-z'])+'\0'+taggedIndex),trackedFingerprint:digest(tracked),untrackedFingerprint:digest(untracked)};
}
export function identity(before,after){
  return before.commit===after.commit&&before.indexFingerprint===after.indexFingerprint&&before.trackedFingerprint===after.trackedFingerprint&&before.untrackedFingerprint===after.untrackedFingerprint;
}
export function combinedVerdict(mode,reports,workerExits){
  if(reports.some(r=>r.checksVerdict==='failed'))return 'failed';
  if(!workerExits||Object.keys(workerExits).length!==2||['windows','linux'].some(p=>!Number.isInteger(workerExits[p])))return 'blocked';
  // Exit 2 with an explicitly blocked worker report is a normal blocker.
  // Any other nonzero exit, including a success report followed by a crash,
  // is a process failure. Neither can be overridden by a passing report.
  if(['windows','linux'].some(p=>workerExits[p]!==0&&!(workerExits[p]===2&&reports.find(r=>r.platform===p)?.checksVerdict==='blocked')))return 'failed';
  if(Object.values(workerExits).some(code=>code!==0))return 'blocked';
  if(new Set(reports.map(r=>r.platform)).size!==2||!reports.some(r=>r.platform==='windows')||!reports.some(r=>r.platform==='linux'))return 'blocked';
  if(reports.length!==2||reports.some(r=>r.checksVerdict==='blocked'))return 'blocked';
  if(reports.some(r=>r.checksVerdict!=='passed'||!r.sourceBefore||!r.sourceAfter))return 'blocked';
  if(reports[0].sourceBefore.commit!==reports[1].sourceBefore.commit)return 'blocked';
  if(reports.some(r=>!identity(r.sourceBefore,r.sourceAfter)))return 'blocked';
  if(reports.some(r=>!r.sourceFinal||!identity(r.sourceBefore,r.sourceFinal)))return 'blocked';
  if(reports.some(r=>!r.harness)||JSON.stringify(reports[0].harness)!==JSON.stringify(reports[1].harness))return 'blocked';
  if(reports.some(r=>[r.sourceBefore,r.sourceAfter,r.sourceFinal].some(s=>!Array.isArray(s.indexFlags))))return 'blocked';
  if(reports.some(r=>r.steps.some(s=>s.status==='passed'&&testBaselines[s.id]&&(!s.testResults?.complete||!Number.isSafeInteger(s.testResults.passed)||s.testResults.passed<testBaselines[s.id]||s.testResults.total!==s.testResults.passed||['failed','skipped','cancelled','todo'].some(k=>s.testResults[k]!==0)))))return 'blocked';
  if(mode==='quick')return 'quick_pass';
  if(reports.some(r=>r.mode!=='full'||r.steps.some(s=>s.status!=='passed')))return 'blocked';
  if(reports.some(r=>{const expected=plan(r.platform,'full').map(s=>s.id);const ids=r.steps.map(s=>s.id);return ids.length!==expected.length||new Set(ids).size!==ids.length||expected.some(id=>!ids.includes(id));}))return 'blocked';
  if(reports.some(r=>r.sourceBefore.dirtyTracked.length||r.sourceBefore.untracked.length||r.sourceBefore.indexFlags.length))return 'non_authoritative';
  return 'passed';
}
// A successful process exit alone does not establish that every test executed.
// Inspect the runner's final summary and retain counts in the evidence report.
export function testEvidence(id,output,expectedCount){
  const text=output.replace(/\u001b\[[0-9;]*m/g,'');
  let total,passed,failed=0,skipped=0,cancelled=0,todo=0;
  if(id==='harness-tests'){
    const match=text.match(/(\d+) harness regression checks passed/);
    if(!match)throw Error('Missing harness regression summary');
    total=passed=Number(match[1]);
  }else if(id.endsWith('-ctest')){
    const match=[...text.matchAll(/(\d+)% tests passed(?:, (\d+) tests failed)? out of (\d+)/g)].at(-1);
    if(!match)throw Error('Missing CTest result summary');
    total=Number(match[3]);failed=Number(match[2]??0);passed=total-failed;
    if(total!==expectedCount||Number(match[1])!==100||/\*\*\*(?:Skipped|Not Run|Disabled)/i.test(text))throw Error('Incomplete CTest execution; inspect the full log');
  }else if(id==='controller-test'||id==='windows-desktop-smoke'){
    const count=key=>{const matches=[...text.matchAll(new RegExp('(?:^|\\n)[^\\S\\n]*(?:ℹ\\s*)?'+key+'\\s+(\\d+)\\b','g'))];if(!matches.length)throw Error('Missing Node test summary: '+key);return Number(matches.at(-1)[1]);};
    total=count('tests');passed=count('pass');failed=count('fail');skipped=count('skipped');cancelled=count('cancelled');todo=count('todo');
  }else if(id==='gui-test'){
    const line=[...text.matchAll(/^\s*Tests\s+(.+)$/gm)].at(-1)?.[1];
    const match=line?.match(/^(\d+) passed\s+\((\d+)\)/);
    if(!match)throw Error('Incomplete or missing Vitest summary: '+(line??'unavailable'));
    passed=Number(match[1]);total=Number(match[2]);
  }else if(id==='playwright-e2e'){
    const match=[...text.matchAll(/^\s*(\d+) passed\b/gm)].at(-1);
    if(!match||/^\s*\d+ (?:skipped|did not run|interrupted|failed|flaky)\b/m.test(text))throw Error('Incomplete or missing Playwright summary');
    total=passed=Number(match[1]);
  }else return undefined;
  if(!total||total!==passed||failed||skipped||cancelled||todo)throw Error(`Incomplete test coverage: total=${total}, passed=${passed}, failed=${failed}, skipped=${skipped}, cancelled=${cancelled}, todo=${todo}`);
  const minimumExpected=testBaselines[id];
  if(minimumExpected!==undefined&&passed<minimumExpected)throw Error(`Reduced test coverage for ${id}: ${passed} passed; reviewed minimum is ${minimumExpected}. Review the test selection or explicitly revise the baseline.`);
  return {total,passed,failed,skipped,cancelled,todo,complete:true,...(minimumExpected===undefined?{}:{minimumExpected})};
}
function save(path,value){
  mkdirSync(dirname(path),{recursive:true});const temp=path+'.tmp';
  writeFileSync(temp,JSON.stringify(value,null,2)+'\n');renameSync(temp,path);
}
function markdown(report){
  return `# Local CI: ${report.platform??'combined'}\n\n- Started: ${report.startedAt}\n- Finished: ${report.finishedAt??'running'}\n- Mode: ${report.mode}\n- Commit: ${report.sourceBefore?.commit??report.commit??'unavailable'}\n- Verdict: **${report.verdict??report.checksVerdict??'running'}**\n- Authoritative full pass: ${report.authoritative===true}\n\n`+
    (report.reason?report.reason+'\n\n':'')+
    (report.workflows?'Historical workflow evidence:\n\n'+Object.values(report.workflows).map(w=>`- ${w.path}: ${w.status}${w.sha256?' (SHA-256 '+w.sha256+')':''}`).join('\n')+'\n\n':'')+
    '| Step | Status | Seconds | Command / reason |\n| --- | --- | ---: | --- |\n'+
    (report.steps??[]).map(s=>`| ${s.id} | ${s.status} | ${((s.durationMs??0)/1000).toFixed(3)} | ${JSON.stringify(s.command??s.reason??'').replaceAll('|','\\|')} |`).join('\n')+'\n';
}
function publish(path,r){save(path,r);writeFileSync(path.replace(/\.json$/,'.md'),markdown(r));}
const nativeTests=['m0_smoke','m1_core','m2_evaluation','abi_c','abi_cpp','m3_search','m4_search','m5_search','m72_search','m6_uci','m7_calibration','m7_layout','m76_policy_smoke','m76_policy_build'];
export function plan(os,mode,areas=[]){
  const steps=[];const selected=area=>mode==='full'||areas.includes(area);
  const add=(id,command,cwd='',dependencies=[],area='infrastructure',extra={})=>steps.push({id,command,cwd,dependencies,selected:selected(area),...extra});
  const needsDebug=mode==='full'||areas.some(a=>['native','controller','gui','smoke','e2e'].includes(a));
  add('harness-tests',['node',join(here,'suite.test.mjs')]);
  for(const preset of ['native-debug','native-release','native-profile']){
    const chosen=preset==='native-debug'?needsDebug:mode==='full';
    add(`${preset}-configure`,['cmake','--preset',preset,'-DBUILD_TESTING=ON',`-DCHESS_LAB_M6_NODE=${process.execPath}`],'',[],'native',{selected:chosen,compiler:true});
    add(`${preset}-build`,['cmake','--build','--preset',preset],'',[`${preset}-configure`],'native',{selected:chosen});
    add(`${preset}-inventory`,['ctest','--preset',preset,'--show-only=json-v1'],'',[`${preset}-build`],'native',{selected:chosen,inventory:nativeTests});
    add(`${preset}-ctest`,['ctest','--preset',preset,'--output-on-failure','--no-tests=error'],'',[`${preset}-inventory`],'native',{selected:chosen,build:preset});
  }
  const controller=mode==='full'||areas.some(a=>['controller','gui','smoke','e2e'].includes(a));
  add('controller-ci',['npm','ci','--ignore-scripts'],'controller',[],'controller',{selected:controller});
  for(const task of ['build','typecheck','lint'])add(`controller-${task}`,['npm','run',task],'controller',['controller-ci'],'controller',{selected:controller,build:'native-debug'});
  const gui=mode==='full'||areas.some(a=>['gui','smoke','e2e'].includes(a));
  const guiAssets=gui||controller;
  add('gui-ci',['npm','ci','--ignore-scripts'],'gui',[],'gui',{selected:guiAssets});
  for(const task of ['typecheck','lint','build','test'])add(`gui-${task}`,['npm','run',task],'gui',['gui-ci'],'gui',{selected:task==='build'?guiAssets:gui});
  // Controller tests include the real built-assets desktop assertion.
  add('controller-test',['npm','run','test'],'controller',['controller-ci','controller-build','native-debug-build','gui-build'],'controller',{selected:controller,build:'native-debug'});
  if(os==='windows')add('windows-desktop-smoke',['node','--test','--test-isolation=none','--test-reporter=spec','controller/test/m7-desktop-smoke.test.mjs'],'',['controller-build','gui-build'],'smoke',{selected:mode==='full'||areas.includes('smoke')});
  if(os==='linux'){
    add('playwright-install',['npm','exec','--no','--','playwright','install','chromium'],'gui',['gui-ci'],'e2e');
    add('playwright-e2e',['npm','run','e2e'],'gui',['playwright-install','gui-build','controller-build','native-debug-build'],'e2e',{build:'native-debug'});
    add('wasm-sdk',['sdk-info'],'',[],'wasm',{sdk:true});
    add('wasm-configure',['wasm-configure','-DBUILD_TESTING=ON'],'',['wasm-sdk'],'wasm',{sdk:true});
    add('wasm-build',['cmake','--build','--preset','wasm-debug'],'',['wasm-configure'],'wasm',{sdk:true});
    add('wasm-inventory',['ctest','--preset','wasm-debug','--show-only=json-v1'],'',['wasm-build'],'wasm',{sdk:true,inventory:nativeTests.filter(n=>n!=='m6_uci')});
    add('wasm-ctest',['ctest','--preset','wasm-debug','--verbose','--no-tests=error'],'',['wasm-inventory'],'wasm',{sdk:true});
  }
  return steps;
}
function actualCommand(command){
  if(command[0]==='node')return [process.execPath,...command.slice(1)];
  if(command[0]==='npm'&&process.platform==='win32'){
    const cli=join(dirname(process.execPath),'node_modules/npm/bin/npm-cli.js');
    if(!existsSync(cli))throw Error(`npm CLI missing beside Node: ${cli}`);
    return [process.execPath,cli,...command.slice(1)];
  }
  return command;
}
let activeChild;
let interrupted=false;
for(const signal of ['SIGINT','SIGTERM'])process.on(signal,()=>{
  interrupted=true;
  if(activeChild){
    if(process.platform==='win32')spawnSync('taskkill',['/PID',String(activeChild.pid),'/T','/F']);
    else {try{process.kill(-activeChild.pid,'SIGTERM');}catch{/* already exited */}}
  }
});
export async function execute(command,{cwd,env=process.env,log,onOutput}={}){
  const started=Date.now();
  if(log)writeFileSync(log,`command=${JSON.stringify(command)}\ncwd=${cwd}\n`);
  return await new Promise(resolveRun=>{
    const child=spawn(command[0],command.slice(1),{cwd,env,windowsHide:true,detached:process.platform!=='win32',stdio:['ignore','pipe','pipe']});activeChild=child;
    let output='',stdout='',outputTail='',settled=false;
    const consume=(data,isStdout)=>{
      if(log)appendFileSync(log,data);
      const chunk=data.toString();
      if(output.length<4*1024*1024)output=(output+chunk).slice(0,4*1024*1024);
      if(isStdout&&stdout.length<4*1024*1024)stdout=(stdout+chunk).slice(0,4*1024*1024);
      outputTail=(outputTail+chunk).slice(-128*1024);onOutput?.(data);
    };
    child.stdout.on('data',data=>consume(data,true));child.stderr.on('data',data=>consume(data,false));
    const finish=(code,error)=>{if(settled)return;settled=true;activeChild=null;resolveRun({code,error,output,stdout,outputTail,durationMs:Date.now()-started});};
    child.on('error',e=>finish(null,e.message));child.on('close',(code,signal)=>finish(code,signal?`signal ${signal}`:undefined));
  });
}
function tool(command,args=['--version']){
  const started=Date.now();
  const r=spawnSync(command,args,{encoding:'utf8',windowsHide:true});
  return {command:[command,...args],exitCode:r.status,durationMs:Date.now()-started,error:r.error?.message,version:[r.stderr,r.stdout].filter(s=>s?.trim()).map(s=>s.trim()).join('\n')};
}
async function run(options){
  const {repo,report:reportPath,platform:os,mode}=options;
  const r={schemaVersion:1,platform:os,mode,startedAt:new Date().toISOString(),host:{platform:platform(),release:release(),cpu:cpus()[0]?.model},steps:[],toolchains:{},testBaselines,authoritative:false};
  const flush=()=>publish(reportPath,r);flush();
  try{
    if(!/^v24\./.test(process.version))throw Error('Node 24 is required for application suites');
    if((os==='windows')!==(process.platform==='win32'))throw Error('Worker platform does not match host');
    r.sourceBefore=snapshot(repo);
    r.harness=Object.fromEntries(['run-all.ps1','run-linux.sh','suite.mjs','suite.test.mjs'].map(p=>[p,sha(readFileSync(join(here,p)))]));
    r.workflows=workflowEvidence(repo);
    const npmVersionCommand=actualCommand(['npm','--version']);
    r.toolchains={node:process.version,git:tool('git'),npm:tool(npmVersionCommand[0],npmVersionCommand.slice(1)),cmake:tool('cmake'),ctest:tool('ctest'),ninja:tool('ninja'),compiler:os==='windows'?tool('cl',['/?']):tool('g++')};
    const steps=plan(os,mode,options.areas.split(',').filter(Boolean));
    r.steps=steps.map(s=>({...s,status:s.selected?'pending':'not_selected'}));flush();
    for(const step of r.steps){
      if(!step.selected)continue;
      step.startedAt=new Date().toISOString();
      const unmet=step.dependencies.find(id=>r.steps.find(s=>s.id===id)?.status!=='passed');
      if(interrupted||unmet||(step.compiler&&(r.toolchains.compiler.exitCode!==0||(os==='windows'&&process.env.VSCMD_ARG_TGT_ARCH!=='x64')))){
        step.status='blocked';step.reason=interrupted?'run interrupted':unmet?`prerequisite ${unmet} did not pass`:'native compiler unavailable; initialize the x64 MSVC developer environment';flush();continue;
      }
      step.log=join(dirname(reportPath),step.id+'.log');
      let command=actualCommand(step.command);
      if(step.sdk){
        const sdk=process.env.CHESS_LAB_CI_EMSDK;
        let args=step.command;
        if(step.id==='wasm-sdk')args=['bash','-c','printf "SDK_ROOT=%s\\nSDK_NODE=%s\\n" "$CHESS_LAB_CI_SDK_ROOT" "$CHESS_LAB_CI_SDK_NODE"; emcc --version; "$CHESS_LAB_CI_SDK_NODE" --version'];
        command=['bash',join(here,'run-linux.sh'),'--emsdk',sdk,'--sdk-exec',...args];
      }
      step.executedCommand=command;
      const env={...process.env,CI:'1'};
      delete env.CHESS_LAB_UCI;delete env.CHESS_LAB_BUILD;delete env.CHROMIUM_EXECUTABLE;
      if(step.build){env.CHESS_LAB_BUILD=join(repo,'build',step.build);env.CHESS_LAB_UCI=join(env.CHESS_LAB_BUILD,os==='windows'?'chess_lab_uci.exe':'chess_lab_uci');}
      env.CHESS_LAB_GUI_ASSETS=join(repo,'gui/dist');
      console.log(`[${os}] ${step.id}: ${JSON.stringify(command)}`);
      const result=await execute(command,{cwd:join(repo,step.cwd),env,log:step.log});
      step.exitCode=result.code;step.durationMs=result.durationMs;step.finishedAt=new Date().toISOString();
      step.status=result.error?'blocked':result.code===0?'passed':'failed';if(result.error)step.reason=result.error;
      if(step.id==='wasm-sdk'&&result.code!==0){step.status='blocked';step.reason='Pinned Emscripten/SDK Node is unavailable or mismatched; see the SDK log.';}
      if(step.inventory&&step.status==='passed'){
        try{const entries=JSON.parse(result.stdout).tests;const tests=entries.map(t=>t.name);step.discoveredTests=tests;const missing=step.inventory.filter(t=>!tests.includes(t));if(missing.length)throw Error('Missing required CTest suites: '+missing.join(', '));if(entries.some(t=>t.properties?.some(p=>p.name==='DISABLED'&&p.value)))throw Error('Disabled CTest suite in inventory');}
        catch(e){step.status='failed';step.reason=e.message;}
      }
      if(step.status==='passed'){
        try{step.testResults=testEvidence(step.id,result.outputTail,r.steps.find(s=>s.id===step.id.replace(/-ctest$/,'-inventory'))?.discoveredTests?.length);}
        catch(e){step.status='blocked';step.reason=e.message;}
      }
      if(step.id==='wasm-sdk'&&step.status==='passed')r.toolchains.wasm=result.output;
      console.log(`[${os}] ${step.id}: ${step.status} (${(step.durationMs/1000).toFixed(2)}s)`);flush();
    }
    r.sourceAfter=snapshot(repo);
    r.checksVerdict=r.steps.some(s=>s.status==='failed')?'failed':r.steps.some(s=>['blocked','pending'].includes(s.status))?'blocked':'passed';
    if(!identity(r.sourceBefore,r.sourceAfter)){if(r.checksVerdict!=='failed')r.checksVerdict='blocked';r.reason='Source changed during validation';}
  }catch(e){r.checksVerdict='blocked';r.reason=e.stack;}
  finally{r.finishedAt=new Date().toISOString();r.durationMs=Date.now()-Date.parse(r.startedAt);flush();}
  return r.checksVerdict==='passed'?0:r.checksVerdict==='failed'?1:2;
}
function combine(options){
  const reports=['windows','linux'].map(p=>{
    try{const report=JSON.parse(readFileSync(join(options.report,p,'report.json'),'utf8'));try{report.sourceFinal=JSON.parse(readFileSync(join(options.report,p+'-final-source.json'),'utf8'));}catch{/* missing final inspection prevents acceptance */}return report;}catch{return {platform:p,checksVerdict:'blocked',reason:'Missing platform report',steps:[]};}
  });
  const coordinatorError=join(options.report,'coordinator-error.json');
  let workerExitEvidence;
  try{workerExitEvidence=JSON.parse(readFileSync(join(options.report,'worker-exits.json'),'utf8'));if(workerExitEvidence.schemaVersion!==1)throw Error('Unsupported worker exit schema');}
  catch(e){workerExitEvidence={error:'Missing or invalid worker exit evidence: '+e.message};}
  const decision=combinedVerdict(options.mode,reports,workerExitEvidence.exits);
  const verdict=decision==='failed'?'failed':existsSync(coordinatorError)?'blocked':decision;
  const result={schemaVersion:1,mode:options.mode,startedAt:reports[0].startedAt,finishedAt:new Date().toISOString(),
    commit:reports[0].sourceBefore?.commit,verdict,authoritative:verdict==='passed',platforms:reports,testBaselines,workerExitEvidence,
    reason:verdict==='non_authoritative'?'Checks passed, but tracked modifications, untracked files or unverified Git index flags prevent clean-commit milestone acceptance.':verdict==='quick_pass'?'Selected checks passed. Quick mode is never full milestone acceptance.':verdict==='passed'?'All full gates passed at the same unchanged clean commit; both worker exits were zero.':'Inspect platform reports and worker exits: failures, blockers, reduced coverage, missing evidence or source identity mismatch prevent a combined pass.',
    steps:reports.flatMap(r=>(r.steps??[]).map(s=>({...s,id:r.platform+'/'+s.id})))};
  result.durationMs=Date.parse(result.finishedAt)-Date.parse(result.startedAt);
  publish(join(options.report,'report.json'),result);console.log(`Combined verdict: ${verdict}. Report: ${join(options.report,'report.md')}`);
  return ['passed','quick_pass'].includes(verdict)?0:verdict==='failed'?1:2;
}
async function main(){
  const [action,...args]=process.argv.slice(2);const options={};
  for(let i=0;i<args.length;i+=2){if(!args[i].startsWith('--')||args[i+1]===undefined)throw Error('Arguments must be --key value pairs');options[args[i].slice(2)]=args[i+1];}
  if(!['full','quick'].includes(options.mode??'full'))throw Error('Invalid mode');options.mode??='full';options.areas??='infrastructure';
  if(action==='inspect'){save(options.report,snapshot(options.repo));return 0;}
  if(action==='blocked'){publish(options.report,{platform:options.platform,mode:options.mode,startedAt:new Date().toISOString(),finishedAt:new Date().toISOString(),checksVerdict:'blocked',reason:options.reason,steps:[]});return 2;}
  if(action==='combine')return combine(options);
  if(action==='run')return await run(options);
  throw Error('Unknown action');
}
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  try{process.exitCode=await main();}catch(e){console.error(e.stack);process.exitCode=2;}
}
