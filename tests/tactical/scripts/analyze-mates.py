#!/usr/bin/env python3
"""Analyze published mate positions and CELab mate-score/PV agreement at each depth."""
import argparse,collections,json,pathlib,statistics,sys,traceback
BASE='f92a170deee6d9e56b173101ad50aca6c619ab6d'

def read(path):
    return [json.loads(x) for x in path.read_text(encoding='utf-8').splitlines() if x.strip()]

def first(rows,predicate):
    return next((x for x in rows if predicate(x)),None)

def semantic(r):
    return (r['bestmove'],r['scoreKind'],r['scoreValue'],[(x['depth'],x['nodes'],x['mateMoves'],x['firstMove'],tuple(x['pv']),x['pvLegal'],x['pvCheckmate']) for x in r['depths']])

def main():
    p=argparse.ArgumentParser()
    for k in ['evidence','depth','reps','mate-from','mate-to']:p.add_argument('--'+k,required=True)
    p.add_argument('--skip-wsl',action='store_true')
    a=p.parse_args();root=pathlib.Path(a.evidence);depth=int(a.depth);reps=int(a.reps);lo=int(a.mate_from);hi=int(a.mate_to)
    corpus=json.loads((root/'frozen-mates.json').read_text(encoding='utf-8'))
    if corpus['baseline']!=BASE or corpus['schema']!=2 or corpus['total']!=32:raise ValueError('Wrong frozen corpus')
    expected={c['id']:c for c in corpus['cases'] if lo<=c['mateIn']<=hi}
    if len(expected)!=4*(hi-lo+1):raise ValueError('Wrong cohort')
    platforms=['windows'] if a.skip_wsl else ['windows','linux']
    byplatform={};summary={};problems=[]
    for platform in platforms:
        rows=read(root/f'{platform}.jsonl')
        if len(rows)!=len(expected)*reps:raise ValueError(f'{platform}: expected {len(expected)*reps}, got {len(rows)}')
        indexed={}
        for r in rows:
            key=(r['rep'],r['id'])
            if r['id'] not in expected or r['rep'] not in range(reps) or key in indexed or r['platform']!=platform:raise ValueError('Unexpected/duplicate row '+str(key))
            f=expected[r['id']];indexed[key]=r
            if r['mateIn']!=f['mateIn'] or r['expectedMove']!=f['expectedMoves'][0]:raise ValueError('Fixture metadata mismatch '+r['id'])
            ds=r['depths']
            if any(x['depth']!=j+1 for j,x in enumerate(ds)) or len(ds)>depth:raise ValueError('Depth order mismatch '+r['id'])
            if not r['timedOut'] and (len(ds)!=depth or not r['completedAllDepths']):raise ValueError('Unreported incomplete search '+r['id'])
            if any(not x['pvLegal'] for x in ds):problems.append({'kind':'illegal_pv','platform':platform,'id':r['id']})
            if r['completedAllDepths'] and not r['timedOut'] and r['finalStatus']!='completed':problems.append({'kind':'bad_final_status','platform':platform,'id':r['id']})
        byplatform[platform]=indexed
        report=[]
        for key,r in sorted(indexed.items(),key=lambda kv:(kv[1]['mateIn'],kv[0][1],kv[0][0])):
            ds=r['depths'];f=expected[r['id']]
            firstReport=first(ds,lambda x:x['mateMoves'] is not None and x['mateMoves']>0)
            firstDist=first(ds,lambda x:x['correctMateDistance'])
            firstPv=first(ds,lambda x:x['pvCheckmate'])
            firstReference=first(ds,lambda x:x['referenceFirstMove'])
            atMax=ds[-1] if ds else None
            report.append({'id':r['id'],'mateIn':f['mateIn'],'rep':r['rep'],'expectedDepth':2*f['mateIn']-1,
               'completedDepth':len(ds),'timedOut':r['timedOut'],'complete':r['completedAllDepths'] and not r['timedOut'],
               'firstMateDepth':firstReport['depth'] if firstReport else None,'firstMateNodes':firstReport['nodes'] if firstReport else None,
               'firstCorrectDistanceDepth':firstDist['depth'] if firstDist else None,'firstCorrectDistanceNodes':firstDist['nodes'] if firstDist else None,
               'firstCheckmatePVDepth':firstPv['depth'] if firstPv else None,'firstCheckmatePVNodes':firstPv['nodes'] if firstPv else None,
               'firstReferenceMoveDepth':firstReference['depth'] if firstReference else None,
               'maxDepthReportedMate':atMax['mateMoves'] if atMax else None,'maxDepthPVCheckmate':atMax['pvCheckmate'] if atMax else False,
               'maxDepthNodes':atMax['nodes'] if atMax else None,'maxDepthPV':atMax['pv'] if atMax else [],
               'maxDepthReferenceMove':atMax['referenceFirstMove'] if atMax else None})
        summary[platform]=report
    mismatches=[];incomplete=[];repeatMismatch=[]
    if 'linux' in byplatform:
        for key,x in byplatform['windows'].items():
            y=byplatform['linux'].get(key)
            if y is None:raise ValueError('Linux missing '+str(key))
            if x['timedOut'] or y['timedOut']:
                incomplete.append({'id':key[1],'rep':key[0],'windowsDepth':len(x['depths']),'linuxDepth':len(y['depths'])})
            elif semantic(x)!=semantic(y):mismatches.append({'id':key[1],'rep':key[0]})
    for p,rows in byplatform.items():
        for id in expected:
            matching=[x for (rep,i),x in rows.items() if i==id and not x['timedOut']]
            if len(matching)>1 and any(semantic(x)!=semantic(matching[0]) for x in matching[1:]):repeatMismatch.append({'platform':p,'id':id})
    stats={}
    for p,results in summary.items():
        stats[p]={}
        for n in range(lo,hi+1):
            group=[x for x in results if x['mateIn']==n]
            stats[p][str(n)]={'attempts':len(group),'completed':sum(x['complete'] for x in group),
              'mateReported':sum(x['firstMateDepth'] is not None for x in group),
              'correctDistanceReported':sum(x['firstCorrectDistanceDepth'] is not None for x in group),
              'checkmatePVFound':sum(x['firstCheckmatePVDepth'] is not None for x in group),
              'referenceFirstMoveAtMaxDepth':sum(x['maxDepthReferenceMove'] is True for x in group),
              'medianNodesAtFullTarget':statistics.median(x['maxDepthNodes'] for x in group if x['complete']) if any(x['complete'] for x in group) else None}
    state=('single_platform_only' if a.skip_wsl and not problems and not repeatMismatch and not incomplete else 'complete') if not problems and not mismatches and not repeatMismatch and not incomplete else ('partial_timeout' if incomplete and not problems and not mismatches and not repeatMismatch else 'review_required')
    report={'schema':2,'kind':'forced_mate','baseline':BASE,'depth':depth,'mateRange':[lo,hi],'reps':reps,'state':state,
       'crossPlatformMismatches':mismatches,'timeoutsNotComparable':incomplete,'repeatMismatches':repeatMismatch,'problems':problems,
       'groupStats':stats,'perPosition':summary,
       'caveats':['Published PV terminates in checkmate, but it is a single variation and is not by itself a proof of forced mate against every defense.',
                  'A mate score is the engine\'s claim of forced mate, not independently validated minimax proof.',
                  'When a time limit interrupts a search, it is not a completed-depth comparison across Windows/WSL.',
                  'Non-detection at depth below 2N-1 does not contradict the published MateTrack mate label.',
                  'Published PV first move agreement is tracked separately; alternative winning moves may also be correct.']}
    (root/'mate-analysis.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    md=['# CELab S0 — Published forced-mate baseline','',f'Baseline `{BASE}`; mate range {lo}–{hi}; requested depth {depth}; repetitions {reps}.','',
        f'Assessment: **{state}**. Timeouts are not a test failure but leave deeper position results unverified.','',
        '| Platform | Mate in | Searches completed | Mate score reported | Correct mate distance | Checkmating PV found | Published first move at last depth |',
        '|---|---:|---:|---:|---:|---:|---:|']
    for platform,values in stats.items():
        for n,s in values.items():md.append(f'| {platform} | {n} | {s["completed"]}/{s["attempts"]} | {s["mateReported"]} | {s["correctDistanceReported"]} | {s["checkmatePVFound"]} | {s["referenceFirstMoveAtMaxDepth"]} |')
    md+=['','### Windows position-level discovery','',
         '| ID | Published mate | Min full-width plies | Completed depth | First mate score depth (nodes) | First correct distance depth (nodes) | First checkmating PV depth (nodes) | Last reported mate |',
         '|---|---:|---:|---:|---|---|---|---:|']
    for r in summary['windows']:
        fmt=lambda prefix: str(r[prefix+'Depth'])+(f' ({r[prefix+"Nodes"]:,})' if r[prefix+'Nodes'] is not None else '') if r[prefix+'Depth'] is not None else '—'
        md.append(f'| {r["id"]} | {r["mateIn"]} | {r["expectedDepth"]} | {r["completedDepth"]}{" timeout" if r["timedOut"] else ""} | {fmt("firstMate")} | {fmt("firstCorrectDistance")} | {fmt("firstCheckmatePV")} | {r["maxDepthReportedMate"] if r["maxDepthReportedMate"] is not None else "—"} |')
    md+=['',f'Cross-platform differences: **{len(mismatches)}**; time-limited noncomparables: **{len(incomplete)}**; repeat differences: **{len(repeatMismatch)}**; other issues: **{len(problems)}**.','',
         '### Interpretation constraints','']+['- '+x for x in report['caveats']]
    (root/'mate-analysis.md').write_text('\n'.join(md)+'\n',encoding='utf8')
    print('\n'.join(md));return 0 if state=='complete' else 2
if __name__=='__main__':
    try:sys.exit(main())
    except Exception:traceback.print_exc();sys.exit(1)
