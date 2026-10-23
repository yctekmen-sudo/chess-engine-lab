// Deterministic harness regressions; no chess assertions or fixtures are altered.
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,mkdirSync,readFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,dirname,resolve,basename} from 'node:path';
import {spawnSync} from 'node:child_process';
import {combinedVerdict as decide,execute,identity,plan,snapshot,testEvidence,testBaselines,workflowEvidence} from './suite.mjs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';

const source={commit:'same',trackedFingerprint:'tracked',untrackedFingerprint:'untracked',dirtyTracked:[],untracked:[],indexFlags:[]};
const complete=passed=>({total:passed,passed,failed:0,skipped:0,cancelled:0,todo:0,complete:true});
const report=os=>({platform:os,mode:'full',sourceBefore:{...source},sourceAfter:{...source},sourceFinal:{...source},harness:{worker:'same'},checksVerdict:'passed',steps:plan(os,'full').map(s=>({...s,status:'passed',...(testBaselines[s.id]?{testResults:complete(testBaselines[s.id])}:{})}))});
const combinedVerdict=(mode,reports,exits={windows:0,linux:0})=>decide(mode,reports,exits);
let checks=0;
const check=(label,fn)=>{fn();++checks;console.log(`PASS ${label}`);};
check('complete clean matrix passes',()=>assert.equal(combinedVerdict('full',[report('windows'),report('linux')]),'passed'));
check('missing platform blocks',()=>assert.equal(combinedVerdict('full',[report('windows')]),'blocked'));
check('different commits block',()=>{const b=report('linux');b.sourceBefore.commit='different';assert.equal(combinedVerdict('full',[report('windows'),b]),'blocked');});
check('tracked dirt is non-authoritative',()=>{const a=report('windows');a.sourceBefore.dirtyTracked=['engine.cpp'];assert.equal(combinedVerdict('full',[a,report('linux')]),'non_authoritative');});
check('untracked sources are non-authoritative',()=>{const a=report('windows');a.sourceBefore.untracked=['extra.test.mjs'];assert.equal(combinedVerdict('full',[a,report('linux')]),'non_authoritative');});
check('mutation during run blocks',()=>{const b=report('linux');b.sourceAfter.trackedFingerprint='changed';assert.equal(combinedVerdict('full',[report('windows'),b]),'blocked');});
check('mutation after first worker blocks',()=>{const b=report('windows');b.sourceFinal.untrackedFingerprint='changed';assert.equal(combinedVerdict('full',[b,report('linux')]),'blocked');});
check('missing final inspection blocks',()=>{const b=report('linux');delete b.sourceFinal;assert.equal(combinedVerdict('full',[report('windows'),b]),'blocked');});
check('different harness revisions block',()=>{const b=report('linux');b.harness={worker:'changed'};assert.equal(combinedVerdict('full',[report('windows'),b]),'blocked');});
check('missing harness identity blocks',()=>{const b=report('linux');delete b.harness;assert.equal(combinedVerdict('full',[report('windows'),b]),'blocked');});
check('missing index-flag verification blocks',()=>{const b=report('linux');b.sourceBefore={...source};delete b.sourceBefore.indexFlags;assert.equal(combinedVerdict('full',[report('windows'),b]),'blocked');});
check('missing worker exit evidence blocks',()=>assert.equal(decide('full',[report('windows'),report('linux')]),'blocked'));
check('missing one worker exit blocks',()=>assert.equal(combinedVerdict('full',[report('windows'),report('linux')],{windows:0}),'blocked'));
check('malformed worker exit evidence blocks',()=>{for(const value of [null,'0',false,0.5])assert.equal(combinedVerdict('full',[report('windows'),report('linux')],{windows:0,linux:value}),'blocked');});
for(const os of ['windows','linux'])check(`${os} abnormal exit overrides successful report`,()=>{
  for(const mode of ['full','quick'])assert.equal(combinedVerdict(mode,[report('windows'),report('linux')],{windows:0,linux:0,[os]:7}),'failed');
});
check('exit 2 cannot override a success report',()=>assert.equal(combinedVerdict('full',[report('windows'),report('linux')],{windows:0,linux:2}),'failed'));
check('ordinary blocked worker exit 2 remains blocked',()=>{const b=report('linux');b.checksVerdict='blocked';assert.equal(combinedVerdict('full',[report('windows'),b],{windows:0,linux:2}),'blocked');});
check('failed command cannot pass',()=>{const b=report('linux');b.checksVerdict='failed';assert.equal(combinedVerdict('full',[report('windows'),b]),'failed');});
check('unavailable dependency cannot pass',()=>{const b=report('linux');b.checksVerdict='blocked';assert.equal(combinedVerdict('full',[report('windows'),b]),'blocked');});
check('a blocker never hides an executed failure',()=>{const a=report('windows');a.checksVerdict='blocked';const b=report('linux');b.checksVerdict='failed';assert.equal(combinedVerdict('full',[a,b]),'failed');});
check('omitted full gate blocks',()=>{const b=report('linux');b.steps.pop();assert.equal(combinedVerdict('full',[report('windows'),b]),'blocked');});
check('quick is never full acceptance',()=>assert.equal(combinedVerdict('quick',[report('windows'),report('linux')]),'quick_pass'));
check('quick reports omitted configurations',()=>{const p=plan('linux','quick',['controller']);assert.equal(p.find(s=>s.id==='native-release-ctest').selected,false);assert.equal(p.find(s=>s.id==='native-debug-ctest').selected,true);assert.equal(p.find(s=>s.id==='controller-test').selected,true);});
check('bounded WASM timeout is not overridden',()=>{const p=plan('linux','full');assert(!p.some(s=>s.command.includes('--timeout')));assert(p.some(s=>s.id==='wasm-inventory'&&s.inventory.includes('m72_search')));});
check('CTest must execute the entire inventory',()=>{assert.equal(testEvidence('wasm-ctest','100% tests passed, 0 tests failed out of 11',11).passed,11);assert.throws(()=>testEvidence('wasm-ctest','100% tests passed, 0 tests failed out of 10',11),/Incomplete/);});
check('CTest Windows success summary is recognized',()=>assert.equal(testEvidence('native-debug-ctest','100% tests passed out of 12',12).passed,12));
check('skipped CTest cannot pass',()=>assert.throws(()=>testEvidence('native-debug-ctest','***Skipped\n100% tests passed, 0 tests failed out of 12',12),/Incomplete/));
const nodeSummary=(skipped=0,count=107)=>`ℹ tests ${count}\nℹ pass ${count-skipped}\nℹ fail 0\nℹ cancelled 0\nℹ skipped ${skipped}\nℹ todo 0\n`;
check('Node test skips prevent acceptance',()=>{assert.equal(testEvidence('controller-test',nodeSummary()).passed,107);assert.throws(()=>testEvidence('controller-test',nodeSummary(1)),/Incomplete/);});
check('Vitest skips prevent acceptance',()=>{assert.equal(testEvidence('gui-test',' Tests 14 passed (14)').passed,14);assert.throws(()=>testEvidence('gui-test',' Tests 14 passed | 1 skipped (15)'),/Incomplete/);});
check('Playwright skips prevent acceptance',()=>{assert.equal(testEvidence('playwright-e2e','  5 passed (10.0s)').passed,5);assert.throws(()=>testEvidence('playwright-e2e','  1 skipped\n  4 passed (10.0s)'),/Incomplete/);});
check('missing test summaries prevent acceptance',()=>assert.throws(()=>testEvidence('controller-test','no tests executed'),/Missing/));
const summary=(id,count)=>id==='harness-tests'?`${count} harness regression checks passed`:id==='gui-test'?`Tests ${count} passed (${count})`:id==='playwright-e2e'?` ${count} passed (1s)`:nodeSummary(0,count);
for(const [id,floor] of Object.entries(testBaselines))check(`${id} coverage floor rejects reductions and allows additions`,()=>{
  assert.throws(()=>testEvidence(id,summary(id,floor-1)),/Reduced test coverage/);
  for(const count of [floor,floor+1])assert.equal(testEvidence(id,summary(id,count)).passed,count);
  const reports=[report('windows'),report('linux')];
  for(const r of reports){const step=r.steps.find(s=>s.id===id);if(step)step.testResults=complete(floor-1);}
  assert.equal(combinedVerdict('full',reports),'blocked');
  for(const r of reports){const step=r.steps.find(s=>s.id===id);if(step)step.testResults=complete(floor+1);}
  assert.equal(combinedVerdict('full',reports),'passed');
});
check('missing application test counts block authoritative acceptance',()=>{const b=report('linux');delete b.steps.find(s=>s.id==='controller-test').testResults;assert.equal(combinedVerdict('full',[report('windows'),b]),'blocked');});
const temp=mkdtempSync(join(tmpdir(),'chess-lab-ci-'));
try{
  const historical=join(temp,'historical workflow evidence');mkdirSync(historical);
  const names=['native','wasm','controller','gui'];
  const absentWorkflows=Object.fromEntries(names.map(name=>[name,{path:`.github/workflows/${name}.yml`,status:'absent'}]));
  check('retired workflow directory absence is explicit and deterministic',()=>{
    assert.deepEqual(workflowEvidence(historical),absentWorkflows);
    assert.equal(JSON.stringify(workflowEvidence(historical)),JSON.stringify(absentWorkflows));
  });
  const workflows=join(historical,'.github/workflows');mkdirSync(workflows,{recursive:true});
  const bytes='historical fixture\n';
  for(const name of names)writeFileSync(join(workflows,name+'.yml'),bytes);
  check('present historical definitions retain ordered SHA-256 evidence',()=>{
    const evidence=workflowEvidence(historical);assert.deepEqual(Object.keys(evidence),names);
    for(const name of names)assert.deepEqual(evidence[name],{path:`.github/workflows/${name}.yml`,status:'present',sha256:createHash('sha256').update(bytes).digest('hex')});
  });
  rmSync(join(workflows,'wasm.yml'));
  check('partial retirement records absence without losing remaining fingerprints',()=>{
    const evidence=workflowEvidence(historical);assert.deepEqual(evidence.wasm,absentWorkflows.wasm);
    for(const name of ['native','controller','gui'])assert.equal(evidence[name].status,'present');
  });
  mkdirSync(join(workflows,'wasm.yml'));
  check('unexpected workflow read errors are not treated as retirement',()=>assert.throws(()=>workflowEvidence(historical),error=>error.code!=='ENOENT'));
  const log=join(temp,'failure.log');
  const bad=await execute([process.execPath,'-e','console.error("intentional failure");process.exit(7)'],{cwd:temp,log});
  check('nonzero child exit preserved',()=>assert.equal(bad.code,7));
  check('stderr preserved',()=>assert.match(readFileSync(log,'utf8'),/intentional failure/));
  const json=await execute([process.execPath,'-e','console.error("SDK setup message");console.log(JSON.stringify({tests:[]}))'],{cwd:temp});
  check('SDK stderr does not contaminate inventory JSON',()=>{assert.deepEqual(JSON.parse(json.stdout),{tests:[]});assert.match(json.output,/SDK setup/);});
  const long=await execute([process.execPath,'-e','process.stdout.write("x".repeat(4*1024*1024+1024));console.log("\\n100% tests passed, 0 tests failed out of 11")'],{cwd:temp});
  check('long output retains the final summary with bounded memory',()=>{assert(long.output.length<=4*1024*1024);assert(long.outputTail.length<=128*1024);assert.equal(testEvidence('wasm-ctest',long.outputTail,11).passed,11);});
  const missing=await execute([join(temp,'missing-executable')],{cwd:temp});
  check('missing executable is unavailable',()=>{assert.equal(missing.code,null);assert(missing.error);});
  const spaced=join(temp,'repo with spaces');mkdirSync(spaced);
  for(const args of [['init','-q'],['config','user.name','Harness Test'],['config','user.email','local-test@example.invalid']])assert.equal(spawnSync('git',args,{cwd:spaced}).status,0);
  writeFileSync(join(spaced,'source.txt'),'original\n');
  assert.equal(spawnSync('git',['add','source.txt'],{cwd:spaced}).status,0);
  assert.equal(spawnSync('git',['commit','-qm','fixture'],{cwd:spaced}).status,0);
  const before=snapshot(spaced);writeFileSync(join(spaced,'source.txt'),'changed\n');const after=snapshot(spaced);
  check('working bytes detect dirty source despite same HEAD',()=>{assert.equal(before.commit,after.commit);assert.deepEqual(after.dirtyTracked,['source.txt']);assert.equal(identity(before,after),false);});
  assert.equal(spawnSync('git',['add','source.txt'],{cwd:spaced}).status,0);
  writeFileSync(join(spaced,'source.txt'),'original\n');
  const staged=snapshot(spaced);
  check('staged changes remain dirty when working bytes match HEAD',()=>{assert.equal(before.trackedFingerprint,staged.trackedFingerprint);assert.deepEqual(staged.dirtyTracked,['source.txt']);assert.equal(identity(before,staged),false);});
  assert.equal(spawnSync('git',['add','source.txt'],{cwd:spaced}).status,0);
  for(const flag of ['assume-unchanged','skip-worktree']){
    const clean=snapshot(spaced);
    assert.equal(spawnSync('git',['update-index','--'+flag,'source.txt'],{cwd:spaced}).status,0);
    const flaggedClean=snapshot(spaced);
    writeFileSync(join(spaced,'source.txt'),'concealed change\n');
    assert.equal(spawnSync('git',['diff','--name-only'],{cwd:spaced,encoding:'utf8'}).stdout,'');
    const indexBefore=readFileSync(join(spaced,'.git/index'));const hidden=snapshot(spaced);
    check(`${flag} cannot conceal modified content as clean and index is preserved`,()=>{
      assert.equal(hidden.dirtyTracked.length,0);assert.equal(hidden.indexFlags.length,1);
      assert.equal(hidden.indexFlags[0][flag==='assume-unchanged'?'assumeUnchanged':'skipWorktree'],true);
      assert.deepEqual(readFileSync(join(spaced,'.git/index')),indexBefore);
      assert.equal(identity(clean,flaggedClean),false);assert.equal(identity(flaggedClean,hidden),false);
      for(const src of [flaggedClean,hidden]){
        const b=report('linux');b.sourceBefore=src;b.sourceAfter=src;b.sourceFinal=src;
        const a=report('windows');a.sourceBefore={...source,commit:src.commit};a.sourceAfter=a.sourceBefore;a.sourceFinal=a.sourceBefore;
        assert.equal(combinedVerdict('full',[a,b]),'non_authoritative');
      }
    });
    writeFileSync(join(spaced,'source.txt'),'original\n');
    assert.equal(spawnSync('git',['update-index','--no-'+flag,'source.txt'],{cwd:spaced}).status,0);
  }
  const suite=fileURLToPath(new URL('./suite.mjs',import.meta.url));
  const combineRepo=join(temp,'successful reports');mkdirSync(combineRepo);
  const writeReports=()=>{for(const os of ['windows','linux']){mkdirSync(join(combineRepo,os),{recursive:true});writeFileSync(join(combineRepo,os,'report.json'),JSON.stringify(report(os)));writeFileSync(join(combineRepo,os+'-final-source.json'),JSON.stringify(source));}};
  for(const os of ['windows','linux']){
    writeReports();
    const child=await execute([process.execPath,'-e','require("node:fs").writeFileSync(process.argv[1],process.argv[2]);process.exit(7)',join(combineRepo,os,'report.json'),JSON.stringify(report(os))],{cwd:temp});
    writeFileSync(join(combineRepo,'worker-exits.json'),JSON.stringify({schemaVersion:1,exits:{windows:0,linux:0,[os]:child.code}}));
    const combined=spawnSync(process.execPath,[suite,'combine','--report',combineRepo,'--mode','full'],{encoding:'utf8'});
    check(`combine CLI rejects ${os} success report followed by exit 7`,()=>{
      assert.equal(child.code,7);assert.equal(combined.status,1,combined.stderr);
      const evidence=JSON.parse(readFileSync(join(combineRepo,'report.json'),'utf8'));
      assert.equal(evidence.authoritative,false);assert.equal(evidence.verdict,'failed');assert.equal(evidence.workerExitEvidence.exits[os],7);
    });
  }
  writeReports();rmSync(join(combineRepo,'worker-exits.json'));
  const absent=spawnSync(process.execPath,[suite,'combine','--report',combineRepo,'--mode','full'],{encoding:'utf8'});
  check('combine CLI rejects absent worker exits despite successful reports',()=>{assert.equal(absent.status,2,absent.stderr);assert.equal(JSON.parse(readFileSync(join(combineRepo,'report.json'),'utf8')).authoritative,false);});
}finally{if(dirname(temp)!==resolve(tmpdir())||!basename(temp).startsWith('chess-lab-ci-'))throw Error('Unsafe fixture cleanup path');rmSync(temp,{recursive:true,force:true});}
assert(checks>=testBaselines['harness-tests'],`Harness baseline ${testBaselines['harness-tests']} exceeds executed checks ${checks}`);
console.log(`${checks} harness regression checks passed`);
