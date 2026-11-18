#!/usr/bin/env node
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {parseEpd,parseLichess,parseCsv,verdict,uci} from './core.mjs';
const cases=[
 'r1bq1r1k/p1pnbpp1/1p2p3/6p1/3PB3/5N2/PPPQ1PPP/2KR3R w - - bm g4; id "arasan2026.1"; c0 "J. Polgar-Berkes, Budapest Hunguest Hotels 2003";',
 '8/6r1/7k/8/8/8/K7/2R5 w - - am Qa7; id "arasan2026.16";',
 '8/6pk/p3p3/2Q1P3/1p2P3/5P1P/5KP1/1q6 w - - id "arasan2026.36"; bm Qb6; c0 "example";'
];
let a=parseEpd(cases[0]);assert.equal(a.expectedType,'best');assert.equal(a.id,'arasan2026.1');assert.deepEqual(a.rawMoves,['g4']);
a=parseEpd(cases[1]);assert.equal(a.expectedType,'avoid');assert.deepEqual(a.rawMoves,['Qa7']);
a=parseEpd(cases[2]);assert.deepEqual(a.rawMoves,['Qb6']);
assert.throws(()=>parseEpd('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - id "x";'));
const samples=parseCsv(fs.readFileSync(new URL('../corpus/lichess-official-samples.csv',import.meta.url),'utf8'));
assert.equal(samples.length,4);for(const x of samples){const p=parseLichess(x);assert(p.solution.length>0);assert(p.setup.length>=4);}
assert.equal(parseLichess(samples[0]).id,'00sHx');
assert.equal(uci({from:'a7',to:'a8',promotion:'q'}),'a7a8q');
assert.equal(verdict({expectedType:'best',expectedMoves:['e2e4']},'e2e4'),true);
assert.equal(verdict({expectedType:'best',expectedMoves:['e2e4']},'d2d4'),false);
assert.equal(verdict({expectedType:'avoid',expectedMoves:['e2e4']},'d2d4'),true);
assert.equal(verdict({expectedType:'avoid',expectedMoves:['e2e4']},'e2e4'),false);
assert.equal(verdict({expectedType:'avoid',expectedMoves:['e2e4']},''),false);
console.log('S0_OFFLINE_SELFTEST_PASS: EPD best/avoid, SAN carriers, Lichess setup, promotion, verdict');
