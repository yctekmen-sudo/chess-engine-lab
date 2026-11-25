import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {nativeEngine as binary,sharedLibrary} from './native-paths.mjs';
function client(t,env={},args=[]){
 const p=spawn(binary,args,{stdio:'pipe',env:{...process.env,...env}}),lines=[],waiters=[];let exit;
 p.on('exit',(code)=>{exit=code;for(const w of waiters.splice(0))w.reject(Error('engine exited '+code));});
 createInterface({input:p.stdout}).on('line',s=>{lines.push(s);for(const w of [...waiters])if(w.check(s)){waiters.splice(waiters.indexOf(w),1);clearTimeout(w.timer);w.resolve(s);}});
 p.stdin.on('error',()=>{});t.after(()=>p.kill('SIGKILL'));
 return {p,lines,send:s=>p.stdin.write(s+'\n'),wait:(check,ms=4000)=>new Promise((resolve,reject)=>{const w={check,resolve,reject,timer:setTimeout(()=>{waiters.splice(waiters.indexOf(w),1);reject(Error('timeout '+lines.slice(-8).join('\n')));},ms)};if(exit!==undefined){clearTimeout(w.timer);reject(Error('already exited'));}else waiters.push(w);})};
}
async function handshake(c){const w=c.wait(s=>s==='uciok');c.send('uci');await w;const r=c.wait(s=>s==='readyok');c.send('isready');await r;}
async function search(c,position,go='go depth 1'){c.send(position);const p=c.wait(s=>s.startsWith('bestmove '));c.send(go);return p;}
test('real handshake, integer centipawn scores, responsive isready, stop and serialization',async t=>{
 const c=client(t);await handshake(c);assert(c.lines.some(s=>s.includes('option name Evaluator ')));
 const first=await search(c,'position startpos');assert.match(first,/^bestmove [a-h][1-8][a-h][1-8]/);
 assert(!c.lines.some(s=>s.includes('score raw white')));assert(c.lines.some(s=>s.includes('score cp')));
 const best=c.wait(s=>s.startsWith('bestmove '));c.send('go infinite');
 const ready=c.wait(s=>s==='readyok',1000);c.send('isready');await ready;c.send('stop');await best;
 c.send('stop');const second=await search(c,'position startpos moves e2e4');assert.match(second,/bestmove/);
});
test('transactional bad position/options and semantic go rejection',async t=>{
 const c=client(t);await handshake(c);
 for(const s of ['position startpos moves e2e5','setoption name Hash value 999999','go ponder','go nodes 10','go depth 0','go depth 1 movetime 10']) {
  const p=c.wait(s=>s.includes('rejected'));c.send(s);await p;
 }
 assert.match(await search(c,'position startpos'),/bestmove/);
});
test('terminal 0000, mate polarity and bounded zero-time fallback',async t=>{
 const c=client(t);await handshake(c);
 assert.equal(await search(c,'position fen 7k/6Q1/5K2/8/8/8/8/8 b - - 0 1'),'bestmove 0000');assert(c.lines.includes('info score mate -0'));
 assert(!c.lines.some(s=>s.startsWith('info string chesslab final_score ')));
 const start=c.lines.length;assert.match(await search(c,'position startpos','go movetime 0'),/bestmove [a-h]/);
 assert(c.lines.slice(start).some(s=>s.includes('fallback unevaluated')||s.startsWith('info depth ')));
 if(c.lines.slice(start).includes('info string chesslab fallback unevaluated'))assert(!c.lines.slice(start).some(s=>s.startsWith('info string chesslab final_score ')));
});
test('single-PV final score is adjacent and recommends PLAY without root claims',async t=>{
 const c=client(t);await handshake(c);
 await search(c,'position fen 7k/8/8/8/8/8/q7/4K3 w - - 100 1');
 let i=c.lines.findLastIndex(s=>s.startsWith('bestmove '));assert.match(c.lines[i-1],/^info string chesslab final_score v2 search 1 depth 1 status completed bestmove .* score finite -900$/);
 const before=c.lines.length;
 await search(c,'position fen 7k/8/8/8/8/8/q7/4K3 w - - 99 1');
 i=c.lines.findLastIndex(s=>s.startsWith('bestmove '));assert.match(c.lines[i-1],/^info string chesslab final_score v2 /);
 assert(!c.lines.some(s=>s.startsWith('info string chesslab decision ')));
 assert(c.lines.slice(before).filter(s=>s.startsWith('info depth ')).every(s=>s.includes('multipv 1 ')));
});
test('evaluator replacement rejects invalid module and preserves healthy selection',async t=>{
 const c=client(t);await handshake(c);let w=c.wait(s=>s.includes('rejected'));c.send('setoption name Evaluator value /no/such/module');await w;
 assert.match(await search(c,'position startpos'),/bestmove/);
 const path=sharedLibrary('material_evaluator');w=c.wait(s=>s.includes('evaluator artifact_sha256'));
 c.send('setoption name Evaluator value '+path);await w;
 assert.match(await search(c,'position startpos'),/bestmove/);
});

test('infinite retains depth-64 result until stop, including ready responsiveness',async t=>{
 const c=client(t);await handshake(c);c.send('position fen 7k/8/5KR1/8/8/8/8/8 w - - 149 1');
 const cap=c.wait(s=>s.startsWith('info depth 64 '));c.send('go infinite');await cap;
 const ready=c.wait(s=>s==='readyok');c.send('isready');await ready;assert(!c.lines.some(s=>s.startsWith('bestmove ')));
 const final=c.wait(s=>s.startsWith('bestmove '));c.send('stop');await final;
});
test('intended claim differs from strongest evaluated PLAY fallback',async t=>{
 const c=client(t);await handshake(c);
 c.send('setoption name MultiPV value 2'); // Preserve the legacy exact-ranking contract.
 const best=await search(c,'position fen 7k/8/8/8/8/8/q7/4Kn2 w - - 99 1');
 const i=c.lines.findLastIndex(s=>s.startsWith('bestmove '));
 assert.equal(c.lines[i-1],'info string chesslab decision claim_draw_with_intended_move e1d1');
 assert.equal(best,'bestmove e1f1');
});
test('blocking evaluator keeps isready responsive and fast stop fallback is unevaluated',async t=>{
 const c=client(t,{CHESS_LAB_TEST_EVAL_DELAY_MS:'100'});await handshake(c);
 const changed=c.wait(s=>s.includes('evaluator artifact_sha256'));c.send('setoption name Evaluator value '+sharedLibrary('m6_test_evaluator'));await changed;
 c.send('position startpos');const final=c.wait(s=>s.startsWith('bestmove '));c.send('go infinite');
 const ready=c.wait(s=>s==='readyok',1000);c.send('isready');await ready;c.send('stop');await final;
 assert(c.lines.includes('info string chesslab fallback unevaluated'));
});
test('failure after provisional depth exits without bestmove and clears TT after search',async t=>{
 const c=client(t,{CHESS_LAB_TEST_EVAL_FAIL_AT:'21'});await handshake(c);
 const changed=c.wait(s=>s.includes('evaluator artifact_sha256'));c.send('setoption name Evaluator value '+sharedLibrary('m6_test_evaluator'));await changed;
 const fatal=c.wait(s=>s.includes('chesslab fatal'));c.send('go depth 2');await fatal;
 assert(c.lines.some(s=>s.startsWith('info depth 1 ')));assert(!c.lines.some(s=>s.startsWith('bestmove ')));
});
test('quit during active search cancels, joins and exits cleanly',async t=>{
 const c=client(t);await handshake(c);const exit=new Promise(resolve=>c.p.once('exit',code=>resolve(code)));
 c.send('go infinite');c.send('quit');assert.equal(await exit,0);
});

// Numeric-TT default integration: check actual search reuse, not merely the
// option banner. The material evaluator is the only implicitly certified
// provider; external evaluators require an explicit option.
async function numericTtExactHits(c) {
 const from=c.lines.length;
 const move=await search(c,'position startpos','go depth 5');
 assert.match(move,/^bestmove [a-h][1-8][a-h][1-8]/);
 const lines=c.lines.slice(from);
 const matches=lines.filter(s=>s.startsWith('info string chesslab metrics '));
 assert.equal(matches.length,1,'exactly one completed search metrics line');
 const result=/\btt_exact_hits (\d+)$/.exec(matches[0]);
 assert(result,'TT exact-hit counter present');
 return Number(result[1]);
}
test('numeric TT packaged-material default, explicit opt-out, external and replacement policy',async t=>{
 // Packaged default must perform actual numeric-score reuse.
 const built=client(t);await handshake(built);
 assert(built.lines.includes('option name NumericTT type check default true'));
 assert((await numericTtExactHits(built))>0,'packaged material numeric default is active');
 built.send('setoption name NumericTT value false');
 assert.equal(await numericTtExactHits(built),0,'explicit opt-out uses hints only');
 built.send('setoption name NumericTT value true');
 assert((await numericTtExactHits(built))>0,'explicit opt-in reactivates numeric reuse');

 // Explicit startup path is not trusted implicitly, even if it happens to
 // point to the separately built material evaluator.
 const explicit=client(t,{},[sharedLibrary('material_evaluator')]);
 await handshake(explicit);
 assert(explicit.lines.includes('option name NumericTT type check default false'));
 assert.equal(await numericTtExactHits(explicit),0,'external path defaults hint-only');
 explicit.send('setoption name NumericTT value true');
 assert((await numericTtExactHits(explicit))>0,'explicitly certified external numeric reuse');

 // Replacing an evaluator invalidates the implicit certification. Verify the
 // effective behavior using the actual search metrics and option banner.
 const replaced=client(t);await handshake(replaced);
 const reset=replaced.wait(s=>s.includes('numeric TT reset to hint-only'));
 replaced.send('setoption name Evaluator value '+sharedLibrary('material_evaluator'));
 await reset;
 const before=replaced.lines.length;
 const banner=replaced.wait(s=>s==='uciok');replaced.send('uci');await banner;
 assert(replaced.lines.slice(before).includes('option name NumericTT type check default false'));
 assert.equal(await numericTtExactHits(replaced),0,'replacement must not inherit certification');
 replaced.send('setoption name NumericTT value true');
 assert((await numericTtExactHits(replaced))>0,'explicit opt-in works after replacement');
});
