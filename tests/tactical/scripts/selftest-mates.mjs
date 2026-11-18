#!/usr/bin/env node
import assert from 'node:assert/strict';
import fs from 'node:fs';
import crypto from 'node:crypto';
const epd=fs.readFileSync(new URL('../corpus/matetrack-selected.epd',import.meta.url),'utf8');
const lines=epd.trim().split(/\r?\n/);
assert.equal(lines.length,32);
let group={};for(const l of lines){let m=l.match(/^\S+\s+[wb]\s+\S+\s+\S+\s+bm\s+#(\d+);\s+PV:\s+([^;]+);$/);assert(m,'bad EPD');let n=Number(m[1]);assert(n>=1&&n<=8);group[n]=(group[n]||0)+1;assert.equal(m[2].trim().split(/\s+/).length,2*n-1)}
assert.deepEqual(group,Object.fromEntries(Array.from({length:8},(_,i)=>[i+1,4])));
assert.equal(crypto.createHash('sha256').update(epd).digest('hex'),'79bdb4ac882aab713096b6b59a5bc07e9c3b4ba61c17f82d1ae606fd40fcdfb3');
console.log('MATE_SELECTION_SELFTEST_PASS: 32 records; four each mate1..8; expected PV lengths; pinned hash');
