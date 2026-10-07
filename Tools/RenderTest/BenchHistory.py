"""Every AI benchmark run so far, in two CSV files for graphs (Tools/RenderTest/Results):

    benchmark_history.csv  one row per course result: when, which run, which build and version, which follower, the course, arrived or
                           not, and the time
    benchmark_summary.csv  one row per run and suite: courses, arrived, success rate, mean time of the arrivals

Run it any time (python Tools/RenderTest/BenchHistory.py); it reads Results/<label>/<build>/results.csv and git, and writes only the two
files. The version of a run is the game's version (Source/System/GameVersion.h) of the code it measured: the last commit before the run
began, or, for a run of work tested before its commit (a commit within the hour after it began), that commit's version, with
tested_before_commit = yes. A run's start is its job.json's, or its first log's time less a suite's run.

Columns to graph by: date (or version_order) on the x axis; suite, follower and condition to split series; success_rate and
mean_seconds to plot. Compare like with like: a suite's course list has changed now and then (courses_in_suite says how many).
"""
import csv
import datetime
import glob
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
RESULTS = os.path.join(HERE, 'Results')
BASELINE_COMMIT = 'bc93d5a5e'  # The original AI, measured from the baseline worktree.


def git(*args):
    return subprocess.run(['git', '-C', REPO, *args], capture_output=True, text=True).stdout.strip()


def version_at(commit):
    text = git('show', f'{commit}:Source/System/GameVersion.h')
    for line in text.splitlines():
        if 'c_VersionString' in line and '"' in line:
            return line.split('"')[1]
    return ''


def follower(label, build):
    """Which movement code the run measured."""
    if build == 'base':
        return 'original AI'
    if label == 'mover2lua':
        return 'Lua follower + engine pilot'
    if label.startswith(('mover', 'foot', 'anchor', 'ladder', 'risk', 'sense', 'steady', 'legs')):
        return 'engine follower'
    if label.startswith('flight4engine'):
        return 'Lua follower + engine pilot'
    return 'Lua follower'


def condition(label):
    return 'no jetpacks' if 'nojet' in label else 'standard'


rows = []
commits = {}
for path in sorted(glob.glob(os.path.join(RESULTS, '*', '*', 'results.csv'))):
    build = os.path.basename(os.path.dirname(path))
    label = os.path.basename(os.path.dirname(os.path.dirname(path)))
    finished = datetime.datetime.fromtimestamp(os.path.getmtime(path))
    started = None
    job = os.path.join(os.path.dirname(path), 'job.json')
    if os.path.exists(job):
        try:
            import json
            with open(job, encoding='utf-8-sig') as jf:
                started = datetime.datetime.strptime(json.load(jf).get('startedAt', ''), '%Y-%m-%d %H:%M:%S')
        except (ValueError, OSError, TypeError):
            started = None
    if started is None:
        logs = [p for p in glob.glob(os.path.join(os.path.dirname(path), '*_*.log'))]
        first = min((os.path.getmtime(p) for p in logs), default=os.path.getmtime(path))
        started = datetime.datetime.fromtimestamp(first) - datetime.timedelta(minutes=2)
    with open(path, encoding='utf-8-sig') as f:
        data = list(csv.DictReader(f))
    if not data:
        continue
    before_commit = 'no'
    if build == 'base':
        commit = BASELINE_COMMIT
        version = version_at(commit) or 'original'
    else:
        commit = git('rev-list', '-1', f'--before={started.isoformat()}', 'ai-overhaul')[:9]
        # (A commit within the hour after the run began: the run measured that work before its commit.)
        after = git('rev-list', '--reverse', f'--since={started.isoformat()}', f'--until={(started + datetime.timedelta(minutes=60)).isoformat()}', 'ai-overhaul').splitlines()
        if after:
            commit = after[0][:9]
            before_commit = 'yes'
        version = version_at(commit) if commit else ''
    commits[(label, build)] = (commit, version)
    for row in data:
        result = row.get('result', '')
        seconds = row.get('seconds', '')
        rows.append({
            'date': finished.strftime('%Y-%m-%d %H:%M'),
            'run_label': label,
            'build': build,
            'version': version,
            'commit': commit,
            'tested_before_commit': before_commit,
            'follower': follower(label, build),
            'condition': condition(label),
            'suite': row.get('suite', ''),
            'course': row.get('course', ''),
            'repeat': row.get('run', ''),
            'arrived': 1 if result.startswith('arrived') or result.startswith('landed') else 0,
            'result': result,
            'seconds': seconds if (result.startswith('arrived') or result.startswith('landed')) else '',
        })

rows.sort(key=lambda r: (r['date'], r['run_label'], r['build'], r['suite'], r['course'], r['repeat']))
history = os.path.join(RESULTS, 'benchmark_history.csv')
with open(history, 'w', newline='', encoding='utf-8') as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    w.writeheader()
    w.writerows(rows)

# The summary: per run, build and suite (and every suite together as "All").
groups = {}
for r in rows:
    for suite in (r['suite'], 'All'):
        key = (r['date'], r['run_label'], r['build'], suite)
        g = groups.setdefault(key, {'results': [], 'times': [], 'courses': set(), 'repeats': set(), 'meta': r})
        g['results'].append(r['arrived'])
        g['courses'].add((r['suite'], r['course']))
        g['repeats'].add(r['repeat'])
        if r['seconds']:
            g['times'].append(float(r['seconds']))
order = {}
for key in sorted({(k[0], k[1], k[2]) for k in groups}):
    order[key] = len(order) + 1
summary = []
for (date, label, build, suite), g in sorted(groups.items()):
    m = g['meta']
    summary.append({
        'date': date,
        'version_order': order[(date, label, build)],
        'run_label': label,
        'build': build,
        'version': m['version'],
        'commit': m['commit'],
        'tested_before_commit': m['tested_before_commit'],
        'follower': m['follower'],
        'condition': m['condition'],
        'suite': suite,
        'courses_in_suite': len(g['courses']),
        'repeats': len(g['repeats']),
        'attempts': len(g['results']),
        'arrived': sum(g['results']),
        'success_rate': round(sum(g['results']) / len(g['results']), 3),
        'mean_seconds': round(sum(g['times']) / len(g['times']), 1) if g['times'] else '',
    })
path = os.path.join(RESULTS, 'benchmark_summary.csv')
with open(path, 'w', newline='', encoding='utf-8') as f:
    w = csv.DictWriter(f, fieldnames=list(summary[0].keys()))
    w.writeheader()
    w.writerows(summary)
print(f'{len(rows)} course results from {len(commits)} runs -> {history}')
print(f'{len(summary)} run and suite rows -> {path}')
