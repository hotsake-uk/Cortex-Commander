"""Every AI benchmark run so far, in two CSV files for graphs (Tools/RenderTest/Results):

    benchmark_history.csv  one row per course result: when, which run, which build and version, which follower, the course, arrived or
                           not, and the time
    benchmark_summary.csv  one row per run and suite: courses, arrived, success rate, mean time of the arrivals
    benchmark_versions.csv one row per version (and follower, where one version measured two): every run of that version pooled,
                           with attempts, arrived, success rate and mean time for each suite in the same columns on every row

Run it any time (python Tools/RenderTest/BenchHistory.py); it reads Results/<label>/<build>/results.csv and git, and writes only the two
files. With --from-history it reads the committed benchmark_history.csv instead and writes only benchmark_versions.csv, for a
checkout without the runs' own results. The version of a run is the game's version (Source/System/GameVersion.h) of the code it measured: the last commit before the run
began, or, for a run of work tested before its commit (a commit within the hour after it began), that commit's version, with
tested_before_commit = yes. A run's start is its job.json's, or its first log's time less a suite's run.

Columns to graph by: date (or version_order) on the x axis; suite, follower and condition to split series; success_rate and
mean_seconds to plot. Compare like with like: a suite's course list has changed now and then (courses_in_suite says how many).
For progress by version, benchmark_versions.csv: version_order on the x axis, a suite's <suite>_success_rate or <suite>_mean_seconds
on the y; a blank means that version never ran the suite. Versions before per-commit numbering (8.0) all read 7.0.0, so that row
pools every run of our AI before then; the original AI has a row of its own.
"""
import csv
import datetime
import glob
import os
import subprocess
import sys

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


# The suites benchmark_versions.csv has columns for, in this order; a no-jetpack suite gets a "_nojet" group of its own.
VERSION_SUITES = ['AIGym', 'Sky', 'Tower', 'AIBywater', 'AIHemslock', 'Flight', 'Recover', 'Tower_nojet']


def version_key(version):
    """Sorts versions numerically (8.1.9 before 8.1.13); a version that isn't numbers sorts first."""
    try:
        return (1, tuple(int(part) for part in version.split('.')))
    except ValueError:
        return (0, (version,))


def write_versions(rows):
    """benchmark_versions.csv: one row per version and follower, every run of it pooled, the same columns on every row."""
    groups = {}
    for r in rows:
        key = (r['version'], r['follower'])
        g = groups.setdefault(key, {'dates': [], 'runs': set(), 'commits': set(), 'before': set(), 'suites': {}})
        g['dates'].append(r['date'])
        g['runs'].add((r['run_label'], r['build']))
        g['commits'].add(r['commit'])
        g['before'].add(r['tested_before_commit'])
        suite = r['suite'] + ('_nojet' if r['condition'] == 'no jetpacks' else '')
        s = g['suites'].setdefault(suite, {'attempts': 0, 'arrived': 0, 'times': []})
        s['attempts'] += 1
        s['arrived'] += int(r['arrived'])
        if r['seconds']:
            s['times'].append(float(r['seconds']))
    # The original AI first, then ours by version; within a version, followers in the order they were first run.
    keys = sorted(groups, key=lambda k: (k[1] != 'original AI', version_key(k[0]), min(groups[k]['dates'])))
    versions = []
    for order, key in enumerate(keys, 1):
        g = groups[key]
        row = {
            'version_order': order,
            'version': key[0],
            'follower': key[1],
            'first_date': min(g['dates']),
            'last_date': max(g['dates']),
            'runs': len(g['runs']),
            'commits': len(g['commits']),
            'commit_list': ' '.join(sorted(g['commits'])),
            'tested_before_commit': 'yes' if g['before'] == {'yes'} else 'some' if 'yes' in g['before'] else 'no',
            'suites_measured': len(g['suites']),
        }
        for suite in VERSION_SUITES + sorted(set(g['suites']) - set(VERSION_SUITES)):
            s = g['suites'].get(suite)
            row[f'{suite}_attempts'] = s['attempts'] if s else ''
            row[f'{suite}_arrived'] = s['arrived'] if s else ''
            row[f'{suite}_success_rate'] = round(s['arrived'] / s['attempts'], 3) if s else ''
            row[f'{suite}_mean_seconds'] = round(sum(s['times']) / len(s['times']), 1) if s and s['times'] else ''
        versions.append(row)
    extra = set().union(*(v.keys() for v in versions)) - set(versions[0].keys())
    if extra:
        raise SystemExit(f'Suites with no column in VERSION_SUITES: {sorted(extra)}; add them there.')
    path = os.path.join(RESULTS, 'benchmark_versions.csv')
    with open(path, 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=list(versions[0].keys()))
        w.writeheader()
        w.writerows(versions)
    print(f'{len(versions)} version rows -> {path}')


if '--from-history' in sys.argv:
    with open(os.path.join(RESULTS, 'benchmark_history.csv'), encoding='utf-8-sig') as f:
        write_versions(list(csv.DictReader(f)))
    sys.exit()


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
write_versions(rows)
