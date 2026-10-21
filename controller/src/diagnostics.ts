import type { SearchLimit } from './engine.js';

export type ProtocolLine={sequence:number;generation:string;at:number;direction:'out'|'in';line:string;session?:string};
export type ReportedStats={depth?:number;nodes?:number;timeMs?:number;nps?:number};
export type SearchEvidence={session:string;requested:SearchLimit;startedAt:number;finishedAt:number;measuredElapsedMs:number;status:'completed'|'interrupted'|'failed';stopRequested:boolean;bestmove?:string;reported:ReportedStats;info:{line:string;at:number;truncated:boolean}[];infoDropped:number};

// UCI string payloads and PV moves are opaque. Optional counters must be whole,
// nonnegative safe integers; a malformed latest value is unavailable, never zero.
export function reportedStats(line:string,previous:ReportedStats={}):ReportedStats|undefined {
  const tokens=line.trim().split(/\s+/);
  if(tokens.shift()!=='info'||tokens[0]==='string')return;
  const result:ReportedStats={...previous};
  const fields={depth:'depth',nodes:'nodes',time:'timeMs',nps:'nps'} as const;
  for(let i=0;i<tokens.length;i++) {
    const token=tokens[i]!;
    if(['string','pv','refutation','currline'].includes(token))break;
    if(Object.hasOwn(fields,token)) {
      const key=fields[token as keyof typeof fields],value=tokens[i+1];
      const n=value!==undefined&&/^\d+$/.test(value)?Number(value):NaN;
      delete result[key];if(Number.isSafeInteger(n))result[key]=n;
    }
  }
  return result;
}

export type AttributedProtocolLine=ProtocolLine&{game:string;attempt:string;engine:0|1;color:'w'|'b';ply?:number};
export class ProtocolCapture {
  readonly entries:AttributedProtocolLine[]=[];
  private observed=0;private bytes=0;private dropped=0;
  constructor(readonly maxEntries:number,readonly maxBytes:number) {}
  add(line:AttributedProtocolLine):void {
    const entry={...line,sequence:++this.observed};
    const bytes=Buffer.byteLength(JSON.stringify(entry));
    // Retain a prefix, even if a later small line would fit. Never imply gaps
    // are a complete transcript. The budget counts full serialized entries.
    if(this.dropped||this.entries.length>=this.maxEntries||this.bytes+bytes>this.maxBytes){this.dropped++;return;}
    this.entries.push(entry);this.bytes+=bytes;
  }
  snapshot(lifecycleComplete:boolean,observerFailures=0) {
    return {entries:structuredClone(this.entries),maxEntries:this.maxEntries,maxBytes:this.maxBytes,retainedBytes:this.bytes,observedEntries:this.observed,droppedEntries:this.dropped,truncated:this.dropped>0,lifecycleComplete,observerFailures,complete:lifecycleComplete&&this.dropped===0&&observerFailures===0};
  }
}

export type MoveSearch=SearchEvidence&{engine:0|1;color:'w'|'b';ply:number;accepted:boolean};
type Range={samples:number;min?:number;max?:number};
export type SearchTotals={engine:0|1;searches:number;completed:number;interrupted:number;failed:number;measuredElapsedMs:number;reported:Record<keyof ReportedStats,Range>};
export const DEFAULT_SEARCH_EVIDENCE_LIMITS=Object.freeze({maxEntries:256,maxBytes:262144});
export function emptySearchTotals(engine:0|1):SearchTotals {
  return {engine,searches:0,completed:0,interrupted:0,failed:0,measuredElapsedMs:0,reported:{depth:{samples:0},nodes:{samples:0},timeMs:{samples:0},nps:{samples:0}}};
}
export function addSearchTotals(total:SearchTotals,search:MoveSearch):void {
  total.searches++;total[search.status]++;total.measuredElapsedMs+=search.measuredElapsedMs;
  for(const field of ['depth','nodes','timeMs','nps'] as const) {
    const value=search.reported[field];if(value===undefined)continue;
    const range=total.reported[field];range.samples++;
    range.min=range.min===undefined?value:Math.min(range.min,value);
    range.max=range.max===undefined?value:Math.max(range.max,value);
  }
}
export function mergeSearchTotals(total:SearchTotals,part:SearchTotals):void {
  for(const field of ['searches','completed','interrupted','failed','measuredElapsedMs'] as const)total[field]+=part[field];
  for(const field of ['depth','nodes','timeMs','nps'] as const) {
    const range=total.reported[field],other=part.reported[field];range.samples+=other.samples;
    if(other.min!==undefined)range.min=range.min===undefined?other.min:Math.min(range.min,other.min);
    if(other.max!==undefined)range.max=range.max===undefined?other.max:Math.max(range.max,other.max);
  }
}

type SearchAttribution=Pick<MoveSearch,'session'|'engine'|'color'|'ply'>;
export class SearchEvidenceCapture {
  private readonly entries:MoveSearch[]=[];
  private readonly totals:[SearchTotals,SearchTotals]=[emptySearchTotals(0),emptySearchTotals(1)];
  private bytes=0;private dropped=0;
  private firstDropped:SearchAttribution|undefined;private lastDropped:SearchAttribution|undefined;
  constructor(readonly maxEntries:number=DEFAULT_SEARCH_EVIDENCE_LIMITS.maxEntries,readonly maxBytes:number=DEFAULT_SEARCH_EVIDENCE_LIMITS.maxBytes) {}
  add(search:MoveSearch):void {
    addSearchTotals(this.totals[search.engine],search);
    // Once the prefix fills, aggregate subsequent searches without serializing
    // their raw info or retaining their sessions. No work scales with history.
    if(!this.dropped&&this.entries.length<this.maxEntries) {
      const bytes=Buffer.byteLength(JSON.stringify(search));
      if(this.bytes+bytes<=this.maxBytes){this.entries.push(structuredClone(search));this.bytes+=bytes;return;}
    }
    this.dropped++;
    const {session,engine,color,ply}=search;this.lastDropped={session,engine,color,ply};
    this.firstDropped??=this.lastDropped;
  }
  snapshot(lifecycleComplete:boolean) {
    return {searches:structuredClone(this.entries),searchEvidence:{maxEntries:this.maxEntries,maxBytes:this.maxBytes,retainedBytes:this.bytes,retainedSearches:this.entries.length,observedSearches:this.totals[0].searches+this.totals[1].searches,droppedSearches:this.dropped,truncated:this.dropped>0,lifecycleComplete,complete:lifecycleComplete&&this.dropped===0,...(this.firstDropped?{firstDropped:{...this.firstDropped},lastDropped:{...this.lastDropped!}}:{}),summary:structuredClone(this.totals)}};
  }
}
