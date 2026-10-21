// Run from any directory: node controller/examples/configure.mjs > work/m6-sample.json
import {resolve} from 'node:path';
import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
const binary=resolve(import.meta.dirname,'../../build/native-debug/chess_lab_uci');
const sha256=createHash('sha256').update(await readFile(binary)).digest('hex');
console.log(JSON.stringify({engines:[{path:binary,kind:'chesslab',sha256,options:{Hash:'16',MultiPV:'1',NumericTT:'false',ValuePolicy:'1'}},{path:process.execPath,args:[resolve(import.meta.dirname,'../test/mock.mjs'),'normal']}],openings:[{initialFen:'7k/8/5KQ1/8/8/8/8/8 w - - 149 1',moves:[],complete:false}],seed:13,limit:{kind:'depth',depth:1},time:{kind:'untimed'},failurePolicy:'technical_forfeit',maxPlies:4,searchCapMs:2000},null,2));
