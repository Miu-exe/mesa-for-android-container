#!/usr/bin/env python3
"""Compare CTS results of RADV against Samsung's driver (the oracle), per case.

usage: cts-diff.py <radv results.csv> <samsung results.csv> [--list CATEGORY]

Categories (only cases present in both files):
  regress   Samsung passes, RADV fails/crashes/hangs/times out  -> RADV bugs to investigate
  missing   Samsung passes, RADV reports NotSupported           -> features RADV doesn't expose
  extra     Samsung NotSupported, RADV runs it                  -> RADV exposes more (check it passes)
  both_bad  neither driver passes                               -> probably test/env, low priority
"""
import collections, csv, sys

GOOD = {'Pass', 'QualityWarning', 'CompatibilityWarning'}

def load(path):
    res = {}
    with open(path) as f:
        for row in csv.reader(f):
            if len(row) >= 2:
                res[row[0]] = row[1]      # last result wins (re-runs after reboots)
    return res

radv, sams = load(sys.argv[1]), load(sys.argv[2])
cat = collections.defaultdict(list)
for case in sorted(radv.keys() & sams.keys()):
    r, s = radv[case], sams[case]
    if s in GOOD and r not in GOOD:
        cat['missing' if r == 'NotSupported' else 'regress'].append((case, r, s))
    elif s == 'NotSupported' and r != 'NotSupported':
        cat['extra'].append((case, r, s))
    elif s not in GOOD and r not in GOOD and not (s == r == 'NotSupported'):
        cat['both_bad'].append((case, r, s))

if '--list' in sys.argv:
    for case, r, s in cat[sys.argv[sys.argv.index('--list') + 1]]:
        print(f'{case},{r},{s}')
    sys.exit(0)

common = radv.keys() & sams.keys()
print(f'cases: radv {len(radv)}, samsung {len(sams)}, common {len(common)}')
for name, results in (('radv', radv), ('samsung', sams)):
    c = collections.Counter(results[k] for k in common)
    print(f'  {name:8s} ' + ', '.join(f'{k} {v}' for k, v in c.most_common()))
for k in ('regress', 'missing', 'extra', 'both_bad'):
    print(f'{k:9s} {len(cat[k])}')
if cat['regress']:
    print('\nRADV regressions by group (Samsung passes):')
    g = collections.Counter('.'.join(c.split('.')[:4]) + ' ' + r for c, r, _ in cat['regress'])
    for k, v in g.most_common(40):
        print(f'  {v:6d}  {k}')
