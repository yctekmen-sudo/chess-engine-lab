import { FideRules, matePossibility, opposite, type ChessOutcome, type Decision, type MateProof, type RecordState, type Side } from './rules.js';
export interface MonotonicClock {now():number}
export const monotonic:MonotonicClock={now:()=>performance.now()};
export type TimeControl={kind:'untimed'}|{kind:'fischer';initialMs:number;incrementMs:number};
export type Outcome=ChessOutcome|{kind:'adjudication_required';reason:string;proof:MateProof}|{kind:'technical_forfeit';reason:string;loser:Side}|{kind:'invalid';reason:string;loser?:Side};
export type Binding={game:string;generation:number;fen:string;history:string;side:Side};
export type FinalSearchScore={kind:'finite';value:number}|{kind:'white_mate'|'black_mate';distance:number};
export type FinalSearch={contract:'chesslab-final-score-v1';search:number;depth:number;status:'completed'|'cancelled';score:FinalSearchScore};
export type Proposal={session:string;binding:Binding;receivedAt:number;decision:Decision;bestmove:string;finalSearch?:FinalSearch};
export class Game {
  private identity=globalThis.crypto.randomUUID();
  get id(){return this.identity;}
  private rules:FideRules;
  private generation=0;
  private start:number;
  private remaining:Record<Side,number>;
  private accepted=new Set<string>();
  private pending:string|undefined;
  private result:Outcome|undefined;
  private paused=false;
  readonly events:unknown[]=[];
  constructor(record:RecordState,time:TimeControl={kind:'untimed'},readonly clock:MonotonicClock=monotonic) {
    this.rules=new FideRules(record);this.time=Object.freeze(structuredClone(time));
    if(time.kind==='fischer' && (![time.initialMs,time.incrementMs].every(n=>Number.isSafeInteger(n)&&n>=0&&n<=2147483647)))throw Error('invalid clock');
    this.remaining={w:time.kind==='fischer'?time.initialMs:Infinity,b:time.kind==='fischer'?time.initialMs:Infinity};
    this.start=clock.now();this.result=this.rules.terminal();
  }
  readonly time:TimeControl;
  fork():Game {
    const g=new Game(this.record(),this.time,this.clock);
    g.identity=this.identity;g.generation=this.generation;g.start=this.start;g.remaining={...this.remaining};
    g.result=this.outcome();g.paused=this.paused;g.pending=this.pending;
    g.accepted=new Set(this.accepted);g.events.push(...structuredClone(this.events));return g;
  }
  checkpoint(at=this.clock.now()):GameCheckpoint {
    const remaining={...this.remaining};
    if(!this.paused&&!this.result)remaining[this.side()]=Math.max(0,remaining[this.side()]-(at-this.start));
    return {record:this.record(),time:this.time,remaining:{w:Number.isFinite(remaining.w)?remaining.w:null,b:Number.isFinite(remaining.b)?remaining.b:null},outcome:this.outcome()??null,paused:this.paused,generation:this.generation,events:structuredClone(this.events)};
  }
  static restore(c:GameCheckpoint,clock:MonotonicClock=monotonic):Game {
    const g=new Game(c.record,c.time,clock);g.remaining={w:c.remaining.w??Infinity,b:c.remaining.b??Infinity};
    if(Object.values(g.remaining).some(n=>n<0||Number.isNaN(n)))throw Error('invalid restored clocks');
    g.result=c.outcome??undefined;g.paused=true;g.generation=c.generation+1;g.events.push(...structuredClone(c.events));return g;
  }
  pause(at=this.clock.now()):void {
    if(!this.paused&&!this.result){if(at>=this.deadline())this.flag(at);else this.remaining[this.side()]-=at-this.start;}
    this.pending=undefined;this.paused=true;this.generation++;
  }
  resume():void {if(!this.paused)throw Error('game is already running');if(this.result)throw Error('game ended');this.paused=false;this.start=this.clock.now();this.generation++;}
  resign(side:Side):void {if(this.result)throw Error('game ended');this.pause();if(!this.result)this.result={kind:'chess',result:side==='w'?'0-1':'1-0',reason:'resignation'};}

  binding():Binding {return {game:this.id,generation:this.generation,fen:this.rules.fen(),history:JSON.stringify(this.rules.record()),side:this.rules.side()};}
  begin(session:string):Binding {
    if(this.result||this.paused||this.pending)throw Error('game is terminal, paused or has an active proposal session');
    this.pending=session;return this.binding();
  }
  record():RecordState {return this.rules.record();}
  fen():string {return this.rules.fen();}
  side():Side {return this.rules.side();}
  outcome():Outcome|undefined {return this.result?structuredClone(this.result):undefined;}
  clocks():Record<Side,number> {return {...this.remaining};}
  deadline():number {return this.start+this.remaining[this.side()];}
  available():number {return Math.max(0,this.deadline()-this.clock.now());}
  fail(reason:string,policy:'pause'|'technical_forfeit'|'invalid'='pause',loser:Side=this.side()):void {
    if(this.result)return;
    this.pending=undefined;this.paused=true;
    this.events.push({kind:'technical_failure',reason,side:loser,at:this.clock.now()});
    if(policy!=='pause')this.result={kind:policy,reason,loser};
  }
  invalidate(reason:string):void {
    if(this.result)return;
    this.pending=undefined;this.paused=true;
    this.events.push({kind:'orchestration_failure',reason,at:this.clock.now()});
    this.result={kind:'invalid',reason};
  }
  uciClock():{wtime:number;btime:number;winc:number;binc:number} {
    if(this.time.kind!=='fischer')throw Error('UCI clock mode requires Fischer clocks');
    const at=this.clock.now(),side=this.side();
    const remaining=(s:Side)=>Math.min(2147483647,Math.max(0,Math.floor(this.remaining[s]-(s===side?at-this.start:0))));
    return {wtime:remaining('w'),btime:remaining('b'),winc:this.time.incrementMs,binc:this.time.incrementMs};
  }
  flag(at=this.clock.now()):Outcome {
    if(this.result)return this.result;
    if(this.paused)throw Error('game is paused');
    if(at<this.deadline())throw Error('clock not expired');
    const loser=this.side();const proof=matePossibility(this.rules,opposite(loser));
    this.remaining[loser]=0;this.pending=undefined;this.generation++;
    this.result=proof.status==='UNDETERMINED'?{kind:'adjudication_required',reason:'ADJUDICATION_REQUIRED',proof}
      :{kind:'chess',result:proof.status==='CANNOT_MATE'?'1/2-1/2':loser==='w'?'0-1':'1-0',reason:'timeout: '+proof.reason};
    this.events.push({kind:'timeout',at,proof});return this.result;
  }
  accept(p:Proposal):void {
    if(this.result||this.paused||p.session!==this.pending||this.accepted.has(p.session)||JSON.stringify(p.binding)!==JSON.stringify(this.binding()))throw Error('stale or mismatched proposal');
    if(!Number.isFinite(p.receivedAt)||p.receivedAt<this.start||p.receivedAt>this.clock.now())throw Error('invalid monotonic receipt');
    this.rules.assertReplay();const replay=new FideRules(this.rules.record());
    // Validate first; invalid output never stops a clock at its receipt instant.
    if(!replay.legal().includes(p.bestmove))throw Error('illegal bestmove alternative');
    if(p.decision.kind==='play') {
      if(p.decision.move!==p.bestmove)throw Error('inconsistent PLAY');replay.play(p.decision.move);
    } else if(!replay.claim(p.decision.kind==='claim_draw_with_intended_move'?p.decision.move:undefined))throw Error('unverified draw claim');
    if(p.receivedAt>=this.deadline()){this.flag(p.receivedAt);return;}
    const side=this.side();
    this.remaining[side]-=p.receivedAt-this.start;
    this.accepted.add(p.session);this.pending=undefined;this.generation++;
    if(p.decision.kind==='play') {
      if(this.time.kind==='fischer')this.remaining[side]+=this.time.incrementMs;
      this.rules=replay;this.result=this.rules.terminal();this.start=this.clock.now();
    } else this.result={kind:'chess',result:'1/2-1/2',reason:p.decision.kind};
    this.events.push({kind:'accepted',session:p.session,decision:p.decision,receivedAt:p.receivedAt,clocks:this.clocks()});
  }
}

export type GameCheckpoint={record:RecordState;time:TimeControl;remaining:{w:number|null;b:number|null};outcome:Outcome|null;paused:boolean;generation:number;events:unknown[]};
