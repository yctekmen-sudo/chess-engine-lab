import {createInterface} from 'node:readline';
import {Chess} from 'chess.js';
const mode=process.argv[2]??'normal';let board=new Chess(),busy=false;
function out(s){process.stdout.write(s+'\r\n');}
function best(){busy=false;out('bestmove '+board.moves({verbose:true}).map(m=>m.from+m.to+(m.promotion??'')).sort()[0]);}
createInterface({input:process.stdin}).on('line',line=>{
  if(line==='uci') {
    if(mode==='no-uci')return;
    if(mode==='partial') {process.stdout.write('id name Chess');setImmediate(()=>{process.stdout.write(' Lab M6\r\nuci');setImmediate(()=>process.stdout.write('ok\r\n'));});}
    else out('id name Chess Lab M6\r\noption name Hash type spin default 16 min 0 max 256\r\nuciok');
  }else if(line==='isready'){if(mode!=='no-ready')out('readyok');}
  else if(line.startsWith('position ')) {
    if(busy){out('bestmove illegal-overlap');return;}
    const parts=line.split(' moves '),root=parts[0].slice(9);board=new Chess(root==='startpos'?undefined:root.slice(4));
    for(const m of parts[1]?.split(' ')??[])board.move({from:m.slice(0,2),to:m.slice(2,4),promotion:m[4]});
  }else if(line.startsWith('go ')) {
    busy=true;
    if(mode==='crash'){process.stderr.write('intentional fixture crash');process.exit(7);}
    if(mode==='huge'){process.stdout.write('x'.repeat(70000));return;}
    if(mode==='hang'||mode==='stop-late')return;
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
