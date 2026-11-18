// Chess-library-independent input parsing and scoring; UCI legality is checked in freeze.mjs.
export function parseEpd(line) {
  const trimmed = line.trim();
  if (!trimmed || trimmed.startsWith('#')) return null;
  const parts = trimmed.match(/^(\S+)\s+([wb])\s+(\S+)\s+(\S+)\s+(.+)$/);
  if (!parts) throw Error('EPD needs four FEN fields and at least one opcode');
  const fen = `${parts[1]} ${parts[2]} ${parts[3]} ${parts[4]} 0 1`;
  const operations = {};
  for (const piece of parts[5].split(';')) {
    const op = piece.trim().match(/^([a-z][a-z0-9_]*)\s*(.*)$/i);
    if (op) {
      if (op[1] in operations) throw Error(`repeated EPD opcode: ${op[1]}`);
      operations[op[1]] = op[2].trim();
    }
  }
  if (Boolean(operations.bm) === Boolean(operations.am)) throw Error('EPD must have exactly one bm or am operation');
  const rawMoves = (operations.bm ?? operations.am).trim().split(/\s+/);
  if (!rawMoves.length || !rawMoves[0]) throw Error('EPD missing expected move');
  if (!/^"[^"]+"$/.test(operations.id || '')) throw Error('EPD missing quoted id');
  return {id: operations.id.slice(1,-1),fen,expectedType:operations.bm?'best':'avoid',rawMoves,
    comment: /^".*"$/.test(operations.c0||'') ? operations.c0.slice(1,-1) : ''};
}
export function parseLichess(row) {
  if (!row || typeof row !== 'object') throw Error('Invalid CSV record');
  for(const k of ['PuzzleId','FEN','Moves','Rating','Themes']) if(!row[k])throw Error('Lichess field absent: '+k);
  const line = row.Moves.trim().split(/\s+/);
  if (line.length<2 || line.some(s=>!/^[a-h][1-8][a-h][1-8][qrbn]?$/.test(s)))throw Error('Invalid Lichess UCI line');
  return {id:row.PuzzleId, fen:row.FEN, setup:line[0], solution:line.slice(1),
    rating:Number(row.Rating), themes:row.Themes.split(/\s+/)};
}
export function parseCsv(text) {
  const rows=[];let row=[],value='',inQuote=false;
  for(let i=0;i<text.length;i++){
    const c=text[i];
    if(c==='"'){if(inQuote && text[i+1]==='"'){value+='"';i++;}else inQuote=!inQuote;}
    else if(c===','&&!inQuote){row.push(value);value='';}
    else if((c==='\n'||c==='\r')&&!inQuote){if(c==='\r'&&text[i+1]==='\n')i++;row.push(value);value='';if(row.some(x=>x.length))rows.push(row);row=[];}
    else value+=c;
  }
  if(inQuote)throw Error('CSV has unterminated quote');
  if(row.length||value){row.push(value);rows.push(row);}
  if(!rows.length)throw Error('Empty CSV');
  const headers=rows.shift().map(x=>x.replace(/^\ufeff/,''));
  return rows.map((r,i)=>{if(r.length!==headers.length)throw Error(`CSV column mismatch on row ${i+2}`);return Object.fromEntries(headers.map((h,j)=>[h,r[j]]));});
}
export const isUci = s => /^[a-h][1-8][a-h][1-8][qrbn]?$/.test(s);
export const uci = move => move.from+move.to+(move.promotion||'');
export function verdict(record, move) {
  if(!isUci(move))return false;
  return record.expectedType==='best' ? record.expectedMoves.includes(move) :
    record.expectedType==='avoid' ? !record.expectedMoves.includes(move) : false;
}
