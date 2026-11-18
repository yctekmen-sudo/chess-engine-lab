#!/usr/bin/env python3
"""CELab S0 tactical-baseline analysis. Works offline on sealed raw JSONL evidence."""
import argparse,collections,hashlib,json,pathlib,statistics,sys,traceback
BASE='f92a170deee6d9e56b173101ad50aca6c619ab6d'

def verify_sha(path,expected):
    return hashlib.sha256(path.read_bytes()).hexdigest()==expected

def read_jsonl(path):
    rows=[]
    for i,line in enumerate(path.read_text(encoding='utf-8').splitlines(),1):
        try:rows.append(json.loads(line))
        except Exception as e:raise ValueError(f'{path}:{i}: invalid JSON') from e
    return rows

def summarise(rows,corpus,depth,reps):
    cases=corpus['cases']; n=corpus['total']; chosen=cases[:n]; ids={c['id']:c for c in chosen};
    expected=len(ids)*reps
    if len(rows)!=expected:raise ValueError(f'Expected {expected} records; got {len(rows)}')
    seen=set();errors=[];strata=collections.defaultdict(list);perdepth=collections.defaultdict(list)
    keyed={}
    for r in rows:
        key=(r['rep'],r['id']);
        if key in seen:errors.append('duplicate '+str(key))
        seen.add(key)
        if r['rep'] not in range(reps) or r['id'] not in ids:errors.append('unknown case '+str(key));continue
        f=ids[r['id']]
        if r['group']!=f['group'] or r['expectedMoves']!=f['expectedMoves']:errors.append('case metadata mismatch '+str(key))
        d=r['depths']
        if len(d)!=depth or sorted(x['depth'] for x in d)!=list(range(1,depth+1)):errors.append('depth gaps '+str(key));continue
        if any(x['hit']!=(x['firstMove'] in f['expectedMoves'] if f['expectedType']=='best' else x['firstMove'] not in f['expectedMoves']) for x in d):errors.append('incorrect verdict '+str(key))
        if r['bestmove']!=d[-1]['firstMove']:errors.append('final PV mismatch '+str(key))
        first=next((x for x in d if x['hit']),None)
        stable=next((x for j,x in enumerate(d) if all(y['hit'] for y in d[j:])),None)
        strata[f['group']].append({'hit':d[-1]['hit'],'nodes':d[-1]['nodes'],'time':d[-1]['elapsedMs'],
           'firstNodes':None if first is None else first['nodes'],
           'stableNodes':None if stable is None else stable['nodes'], 'wallMs':r['wallMs']})
        for x in d:perdepth[(f['group'],x['depth'])].append(x)
        keyed[key]=r
    if errors:raise ValueError('Invalid observations: '+ '; '.join(errors[:15]))
    result={'records':len(rows),'caseCount':n,'depth':depth,'repetitions':reps,'groups':{},'depthLadder':{},'examples':[]}
    for name,data in sorted(strata.items()):
        successful=[x['firstNodes'] for x in data if x['firstNodes'] is not None]
        stable=[x['stableNodes'] for x in data if x['stableNodes'] is not None]
        result['groups'][name]={'n':len(data),'hitsAtMaxDepth':sum(x['hit'] for x in data),'hitRate':sum(x['hit'] for x in data)/len(data),
           'medianMaxDepthNodes':statistics.median(x['nodes'] for x in data),
           'medianMaxDepthElapsedMs':statistics.median(x['time'] for x in data),
           'medianSearchWallMs':statistics.median(x['wallMs'] for x in data),
           'medianFirstHitNodes':statistics.median(successful) if successful else None,
           'medianStableHitNodes':statistics.median(stable) if stable else None}
    for (group,d),data in sorted(perdepth.items()):result['depthLadder'][f'{group}@{d}']={'n':len(data),'hits':sum(x['hit'] for x in data),
      'medianNodes':statistics.median(x['nodes'] for x in data)}
    return result,keyed

def semantic(r):
    return (r['bestmove'],r['scoreKind'],r['scoreValue'],r['evalCalls'],r['ttLookups'],r['ttExactHits'],
       tuple((d['depth'],d['nodes'],d['mateMoves'],d['centipawns'],tuple(d['pv'])) for d in r['depths']))

def main():
    p=argparse.ArgumentParser();p.add_argument('--evidence',required=True);p.add_argument('--depth',type=int,required=True);p.add_argument('--reps',type=int,required=True);
    p.add_argument('--limit',type=int,default=0);p.add_argument('--skip-wsl',action='store_true')
    a=p.parse_args();ev=pathlib.Path(a.evidence);corpus=json.loads((ev/'frozen-corpus.json').read_text(encoding='utf-8'))
    if corpus['baseline']!=BASE or corpus['schema']!=1 or corpus['total']!=204:raise ValueError('Unexpected baseline/corpus identity')
    if a.limit:
        corpus=dict(corpus,cases=corpus['cases'][:a.limit],total=a.limit)
    plat=['windows'] if a.skip_wsl else ['windows','linux']; summaries={};indexed={}
    for platform in plat:
        rows=read_jsonl(ev/f'{platform}.jsonl');
        if any(x.get('platform')!=platform for x in rows):raise ValueError('Incorrect platform identity')
        summaries[platform],indexed[platform]=summarise(rows,corpus,a.depth,a.reps)
    diffs=[];repeat=[]
    if not a.skip_wsl:
        for key,x in indexed['windows'].items():
            y=indexed['linux'].get(key)
            if y is None or semantic(x)!=semantic(y):diffs.append({'case':key[1],'repeat':key[0]})
    for platform in plat:
        byid=collections.defaultdict(list)
        for key,x in indexed[platform].items():byid[key[1]].append(x)
        for id,observations in byid.items():
            if len({semantic(x) for x in observations})>1:repeat.append({'platform':platform,'case':id})
    report={'schema':1,'baseline':BASE,'complete':not diffs and not repeat,'comparisons':{'crossPlatformDifferences':diffs,'repeatDifferences':repeat},
        'platforms':summaries,'limitations':['Exact-published-move hits are proxy tactical accuracy; alternative good moves are not adjudicated.',
       'Arasan EPD FEN omits full move history; draw/repetition in those positions is not fully reconstructed.',
       'Fixed nominal search depth, not equal-node or equal-time budget; do not call it playing-strength evidence.',
       'Lichess four-record sample is a parser smoke test, not a representative puzzle sample.']}
    (ev/'analysis.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    md=['# CELab S0 tactical baseline','',f'Candidate/benchmark baseline: `{BASE}`','',
        f'Corpus: {corpus["total"]} distinct positions; max nominal depth {a.depth}; {a.reps} repetitions per platform.','',
        '| Platform | Group | Cases × repetitions | Hits at max depth | Hit rate | Median nodes | Median search time (ms) |',
        '|---|---|---:|---:|---:|---:|---:|']
    for platform,summary in summaries.items():
        for group,m in sorted(summary['groups'].items()):md.append(f'| {platform} | {group} | {m["n"]} | {m["hitsAtMaxDepth"]} | {100*m["hitRate"]:.1f}% | {m["medianMaxDepthNodes"]:,.0f} | {m["medianMaxDepthElapsedMs"]:.0f} |')
    md.extend(['',f'Cross-platform semantic mismatches: **{len(diffs)}**; within-platform repeat mismatches: **{len(repeat)}**.','',
      '### Important limitations',''] + ['- '+x for x in report['limitations']])
    (ev/'analysis.md').write_text('\n'.join(md)+'\n',encoding='utf-8')
    print('\n'.join(md));return 0 if report['complete'] else 2
if __name__=='__main__':
 try:sys.exit(main())
 except Exception:
    traceback.print_exc();sys.exit(1)
