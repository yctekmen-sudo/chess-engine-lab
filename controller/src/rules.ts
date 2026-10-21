import { Chess, DEFAULT_POSITION, type Color, type Move, type Square } from 'chess.js';
export { DEFAULT_POSITION };
export type Side = Color;
export type RecordState = Readonly<{initialFen:string; moves:readonly string[]; complete:boolean}>;
export type ChessOutcome = {kind:'chess'; result:'1-0'|'0-1'|'1/2-1/2'; reason:string};
export type Decision = {kind:'play'; move:string}|{kind:'claim_draw'}|{kind:'claim_draw_with_intended_move'; move:string};
export const opposite = (s:Side):Side => s==='w'?'b':'w';
export const uci = (m:Move):string => m.from+m.to+(m.promotion??'');
export function parseMove(s:string):{from:string;to:string;promotion?:string} {
  if(!/^[a-h][1-8][a-h][1-8][qrbn]?$/.test(s))throw Error('invalid UCI move: '+s);
  return {from:s.slice(0,2),to:s.slice(2,4),...(s[4]?{promotion:s[4]}:{})};
}
export interface Rules {
  fen():string; side():Side; legal():string[]; signature():string; occurrences():number;
  terminal():ChessOutcome|undefined; claim(move?:string):boolean; play(move:string):void;
  record():RecordState;
}
export class FideRules implements Rules {
  private board:Chess;
  private rawEp:string;
  private readonly initialFen:string;
  private readonly moves:string[]=[];
  private readonly signatures:string[]=[];
  readonly complete:boolean;
  constructor(record:RecordState={initialFen:DEFAULT_POSITION,moves:[],complete:true}) {
    const fields=record.initialFen.trim().split(/\s+/);
    if(fields.length!==6||!/^\d+$/.test(fields[4]!)||!/^\d+$/.test(fields[5]!))throw Error('invalid six-field FEN');
    this.board=new Chess(fields.join(' '));this.rawEp=fields[3]!;
    const rights=fields[2]!;
    if(rights!=='-'){
      if(!/^[KQkq]+$/.test(rights)||new Set(rights).size!==rights.length)throw Error('invalid castling rights');
      for(const flag of rights){const white=flag===flag.toUpperCase(),king=(white?'e1':'e8') as Square;
        const rook=({K:'h1',Q:'a1',k:'h8',q:'a8'} as Record<string,Square>)[flag]!;
        if(this.board.get(king)?.type!=='k'||this.board.get(king)?.color!==(white?'w':'b')||this.board.get(rook)?.type!=='r'||this.board.get(rook)?.color!==(white?'w':'b'))throw Error('castling rights without original king/rook');
      }
    }
    const whiteKing=this.board.findPiece({type:'k',color:'w'})[0]!,blackKing=this.board.findPiece({type:'k',color:'b'})[0]!;
    if(Math.abs(whiteKing.charCodeAt(0)-blackKing.charCodeAt(0))<=1&&Math.abs(Number(whiteKing[1])-Number(blackKing[1]))<=1)throw Error('adjacent kings');
    if(this.board.isAttacked(this.board.turn()==='w'?blackKing:whiteKing,this.board.turn()))throw Error('side just moved left its king attacked');
    if(this.rawEp!=='-'){
      const white=this.board.turn()==='w',rank=white?'6':'3';
      if(!new RegExp('^[a-h]'+rank+'$').test(this.rawEp))throw Error('invalid raw en passant');
      const pawn=(this.rawEp[0]+(white?'5':'4')) as Square,origin=(this.rawEp[0]+(white?'7':'2')) as Square;
      if(this.board.get(this.rawEp as Square)||this.board.get(origin)||this.board.get(pawn)?.type!=='p'||this.board.get(pawn)?.color!==(white?'b':'w')||Number(fields[4])!==0)throw Error('inconsistent raw en passant');
    }
    if(!Number.isSafeInteger(Number(fields[4]))||Number(fields[4])<0||Number(fields[4])>0xffffffff||!Number.isSafeInteger(Number(fields[5]))||Number(fields[5])<1||Number(fields[5])>0xffffffff)throw Error('FEN counters outside ChessCore bounds');
    this.initialFen=this.fen();
    this.complete=record.complete;this.signatures.push(this.signature());
    for(const move of record.moves)this.play(move);
  }
  fen():string {const fields=this.board.fen({forceEnpassantSquare:true}).split(' ');fields[3]=this.rawEp;return fields.join(' ');}
  side():Side {return this.board.turn();}
  legal():string[] {return this.board.moves({verbose:true}).map(uci).sort();}
  signature():string {return this.board.fen().split(' ').slice(0,4).join(' ');}
  occurrences():number {return this.signatures.filter(s=>s===this.signature()).length;}
  record():RecordState {return {initialFen:this.initialFen,moves:[...this.moves],complete:this.complete};}
  dead():boolean {
    // Same sufficient cases as the baseline ChessCore, independently expressed.
    const pieces=this.board.board().flat().filter(p=>p && p.type!=='k');
    if(pieces.length===0)return true;
    if(pieces.length===1)return pieces[0]!.type==='b'||pieces[0]!.type==='n';
    if(pieces.length===2 && pieces.every(p=>p!.type==='b')) {
      const colors=pieces.map(p=>(p!.square.charCodeAt(0)+Number(p!.square[1]))%2);
      return colors[0]===colors[1];
    }return false;
  }
  terminal():ChessOutcome|undefined {
    if(this.legal().length===0)return this.board.isCheck()
      ?{kind:'chess',result:this.side()==='w'?'0-1':'1-0',reason:'checkmate'}
      :{kind:'chess',result:'1/2-1/2',reason:'stalemate'};
    if(this.dead())return {kind:'chess',result:'1/2-1/2',reason:'known dead position'};
    if(this.occurrences()>=5)return {kind:'chess',result:'1/2-1/2',reason:'fivefold repetition'};
    if(Number(this.fen().split(' ')[4])>=150)return {kind:'chess',result:'1/2-1/2',reason:'75 move rule'};
    return undefined;
  }
  claim(move?:string):boolean {
    if(this.terminal())return false;
    if(move===undefined)return this.occurrences()>=3 || Number(this.fen().split(' ')[4])>=100;
    const probe=new FideRules(this.record());probe.play(move);
    // The announced move is unplayed. Eligibility follows M3's prospective
    // signature/halfmove check, independently of a terminal after actual play.
    return probe.occurrences()>=3 || Number(probe.fen().split(' ')[4])>=100;
  }
  play(move:string):void {
    const m=this.board.move(parseMove(move));
    const fields=this.board.fen().split(' ');
    if(Number(fields[4])>0xffffffff||Number(fields[5])>0xffffffff){this.board.undo();throw Error('chess state counter exhausted');}
    this.rawEp=m.piece==='p'&&Math.abs(Number(m.to[1])-Number(m.from[1]))===2?m.from[0]+String((Number(m.from[1])+Number(m.to[1]))/2):'-';
    this.moves.push(move);this.signatures.push(this.signature());
  }
  assertReplay():void {
    const replay=new FideRules(this.record());
    if(replay.fen()!==this.fen()||replay.signature()!==this.signature()||replay.occurrences()!==this.occurrences())throw Error('replay mismatch');
  }
  pgn(outcome?:ChessOutcome,headers:Record<string,string>={}):string {
    const b=new Chess(this.initialFen);
    for(const m of this.moves)b.move(parseMove(m));
    b.setHeader('Event','Chess Lab M6');
    for(const [k,v] of Object.entries(headers))b.setHeader(k,v);
    b.setHeader('Result',outcome?.result??'*');return b.pgn();
  }
}
export type MateProof = {status:'CAN_MATE';reason:string;witness:string[]}|{status:'CANNOT_MATE'|'UNDETERMINED';reason:string};
export function matePossibility(rules:FideRules,side:Side,maxPlies=3,maxNodes=1000):MateProof {
  const board=new Chess(rules.fen());
  if(board.board().flat().filter(p=>p?.color===side&&p.type!=='k').length===0)
    return {status:'CANNOT_MATE',reason:'bare king cannot give checkmate'};
  if(rules.dead())return {status:'CANNOT_MATE',reason:'certified global dead material class'};
  // All bishops confined to one square color cannot mate, even after promotion
  // produced more bishops. No pawn/knight/rook/queen exists to change that class.
  const material=board.board().flat().filter(p=>p&&p.type!=='k');
  if(material.every(p=>p!.type==='b') && new Set(material.map(p=>(p!.square.charCodeAt(0)+Number(p!.square[1]))%2)).size===1)
    return {status:'CANNOT_MATE',reason:'only same-square-color bishops and kings'};
  let nodes=0;
  function visit(r:FideRules,depth:number):string[]|undefined {
    if(++nodes>maxNodes)return undefined;
    const end=r.terminal();
    if(end)return end.reason==='checkmate'&&r.side()!==side?[]:undefined;
    if(depth===0)return undefined;
    for(const m of r.legal()) {
      if(nodes>=maxNodes)break;
      const next=new FideRules(r.record());next.play(m);
      const tail=visit(next,depth-1);if(tail)return [m,...tail];
    }return undefined;
  }
  const witness=visit(rules,maxPlies);
  return witness?{status:'CAN_MATE',reason:'replayable cooperative legal checkmate witness',witness}
    :{status:'UNDETERMINED',reason:`no proof within ${maxPlies} plies / ${maxNodes} nodes; bound exhaustion is not impossibility`};
}
