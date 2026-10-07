# Compares benchmark runs (Bench.ps1): per course, how often each build arrived and its median time; per suite and overall, the totals.
# Usage: python Bench.py <a/results.csv> <b/results.csv> [more...]
import csv, sys, statistics
from collections import defaultdict
data = defaultdict(lambda: defaultdict(list))  # (suite, course) -> build -> [(result, seconds)]
builds = []
for path in sys.argv[1:]:
    for row in csv.DictReader(open(path, encoding='utf-8-sig')):
        data[(row['suite'], row['course'])][row['build']].append((row['result'], float(row['seconds'])))
        if row['build'] not in builds:
            builds.append(row['build'])
def cell(results):
    if not results:
        return '-'
    arrived = [s for r, s in results if r == 'arrived']
    med = f'{statistics.median(arrived):.1f}s' if arrived else '-'
    return f'{len(arrived)}/{len(results)} {med}'
print('| Suite | Course | ' + ' | '.join(builds) + ' |')
print('|---|---|' + '---|' * len(builds))
totals = defaultdict(lambda: [0, 0])
suite_totals = defaultdict(lambda: defaultdict(lambda: [0, 0]))
for (suite, course), by in sorted(data.items()):
    print(f'| {suite} | {course} | ' + ' | '.join(cell(by.get(b, [])) for b in builds) + ' |')
    for b in builds:
        res = by.get(b, [])
        a = sum(1 for r, _ in res if r == 'arrived')
        totals[b][0] += a; totals[b][1] += len(res)
        suite_totals[suite][b][0] += a; suite_totals[suite][b][1] += len(res)
print()
print('| Suite | ' + ' | '.join(builds) + ' |')
print('|---|' + '---|' * len(builds))
for suite in sorted(suite_totals):
    print(f'| {suite} | ' + ' | '.join(f'{suite_totals[suite][b][0]}/{suite_totals[suite][b][1]}' for b in builds) + ' |')
print('| **All** | ' + ' | '.join(f'**{totals[b][0]}/{totals[b][1]}**' for b in builds) + ' |')
