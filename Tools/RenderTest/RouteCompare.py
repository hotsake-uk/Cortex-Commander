# Lines up two ROUTECMP logs (our build and the original AI's) and lists the routes ours refuses that the original takes, grouped by
# where ours stops. Usage: python RouteCompare.py <ours.log> <base.log>
import re, sys
from collections import Counter
def read(path):
    out = {}
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.search(r'ROUTECMP (\d+) (\d+) (\S+) (\S+) (\w+) (-?\d+)', line)
        if m:
            out[(int(m.group(1)), int(m.group(2)))] = (m.group(3), m.group(4), m.group(5), int(m.group(6)))
    return out
ours, base = read(sys.argv[1]), read(sys.argv[2])
both = [k for k in ours if k in base]
refused = [k for k in both if ours[k][2] in ('cut', 'none') and base[k][2] == 'ok']
print(f'pairs {len(both)}; ours refuses {sum(1 for k in both if ours[k][2] in ("cut","none"))}; original impossible {sum(1 for k in both if base[k][2] in ("wall","none"))}')
print(f'ours refuses but the original takes: {len(refused)}')
starts = Counter(ours[k][0] for k in refused)
goals = Counter(ours[k][1] for k in refused)
print('most common starts:', starts.most_common(8))
print('most common goals:', goals.most_common(8))
for k in refused[:12]:
    print(' ', k, ours[k][0], '->', ours[k][1])
