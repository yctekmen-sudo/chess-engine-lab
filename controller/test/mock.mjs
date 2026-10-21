import {createInterface} from 'node:readline';
import {Chess} from 'chess.js';
import {existsSync,readFileSync,writeFileSync,appendFileSync} from 'node:fs';
const mode=process.argv[2]??'normal';let board=new Chess(),busy=false;
function out(s){process.stdout.write(s+'\r\n');}
function best(){busy=false;out('bestmove '+board.moves({verbose:true}).map(m=>m.from+m.to+(m.promotion??'')).sort()[0]);}
createInterface({input:process.stdin}).on('line',line=>{
  if(mode.startsWith('diagnostics')&&process.argv[3])appendFileSync(process.argv[3],line+'\n');
  if(line==='uci') {
    if(mode==='no-uci')return;
    if(mode==='startup-fail'||mode==='identity-drift') {
      const marker=process.argv[3],n=existsSync(marker)?Number(readFileSync(marker,'utf8'))+1:1;writeFileSync(marker,String(n));
      if(n>2){if(mode==='startup-fail'){process.stderr.write('fixture startup failure');process.exit(9);}out('id name changed after preflight\r\nuciok');return;}
    }
    if(mode==='partial') {process.stdout.write('id name Chess');setImmediate(()=>{process.stdout.write(' Lab M6\r\nuci');setImmediate(()=>process.stdout.write('ok\r\n'));});}
    else out('id name Chess Lab M6\r\noption name Hash type spin default 16 min 0 max 256\r\nuciok');
  }else if(line==='isready'){if(mode!=='no-ready')out('readyok');}
  else if(line.startsWith('position ')) {
    if(busy){out('bestmove illegal-overlap');return;}
    const parts=line.split(' moves '),root=parts[0].slice(9);board=new Chess(root==='startpos'?undefined:root.slice(4));
    for(const m of parts[1]?.split(' ')??[])board.move({from:m.slice(0,2),to:m.slice(2,4),promotion:m[4]});
  }else if(line.startsWith('go ')) {
    busy=true;
    if(mode.startsWith('diagnostics')) {
      out('info string received '+line);
      if(mode==='diagnostics-missing')out('info depth 3 nodes malformed time -1');
      else out('info depth 2 nodes 123 time 7 nps 17571 pv a2a3');
      if(mode==='diagnostics-flood')for(let i=0;i<40;i++)out('info depth 2 nodes '+i+' time 7');
      if(mode==='diagnostics-stop')return;
    }
    if(mode==='clock-trace')out('info string received_go '+line);
    if(mode==='clock-log')appendFileSync(process.argv[3],line+'\n');
    if(mode==='crash'){process.stderr.write('intentional fixture crash');process.exit(7);}
    if(mode==='huge'){process.stdout.write('x'.repeat(70000));return;}
    if(mode==='hang'||mode==='stop-late'){out('info string fixture searching');return;}
    if(mode==='bad'){out('bestmove e9e4');return;}
    if(mode==='illegal'){out('bestmove e2e5');return;}
    if(mode==='spoof'||mode==='nonadjacent'||mode==='conflict')out('info string chesslab decision claim_draw');
    if(mode==='nonadjacent')out('info depth 1 nodes 1');
    if(mode==='conflict')out('info string chesslab decision claim_draw');
    best();if(mode==='duplicate')out('bestmove a2a3');
    if(mode==='late')setImmediate(()=>out('bestmove a2a3'));
  }else if(line==='stop') {
    if(mode==='stop-late')setTimeout(best,200);else if(mode!=='hang'&&busy)best();
  }else if(line==='quit')process.exit(0);
});
