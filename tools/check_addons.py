"""Check the addon split (see docs/addons.md and issue #18).

Addon kinds:
  core     ELifeRPG_Core: base framework. Depends on vanilla and base-content addons only.
  base     ELifeRPG_Assets, ... in addons/: base content of Core, art only, depends on vanilla only.
  dlc      every addon in addons/dlcs/: content plus its own scripts. Depends on Core, never on
           another DLC, and nothing but top-level addons may depend on it.
  top      ELifeRPG (sample world addon) and ELifeRPG_Dev.

Checks:
  - every {GUID} reference resolves inside the referencing addon or its (transitive) dependencies
  - no duplicate GUIDs, no references whose GUID is unknown but whose path exists in an addon
  - dependency direction per kind (see above)
  - base-content addons hold no logic (scripts, prefabs, configs, layouts, worlds)
  - vanilla full-file overrides exist only in ELifeRPG_Core
Warnings (do not fail): .meta files whose Name path differs from the file's real path.

Usage: python3 tools/check_addons.py addons   (exit code 1 on problems)
"""
import re, sys, pathlib

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'addons')
GUID = r'[0-9A-F]{16}'
ref_re = re.compile(rb'\{(' + GUID.encode() + rb')\}([A-Za-z0-9_./ -]+)')
LOGIC_SUFFIXES = ('.c', '.et', '.conf', '.layout', '.ent', '.layer')
SKIP_SUFFIXES = ('.meta', '.rdb')
VANILLA_OVERRIDES = ('Configs/System/chimeraMenus.conf', 'Configs/System/chimeraInputCommon.conf')
TOP_NAMES = {'ELifeRPG', 'ELifeRPG_Dev'}

problems = 0
def problem(msg):
    global problems
    problems += 1
    print(msg)

addons = {}
gprojs = [(gp, 'dlc') for gp in root.glob('dlcs/*/*.gproj')] + [(gp, None) for gp in root.glob('*/*.gproj')]
for gp, kind in gprojs:
    t = gp.read_text(errors='ignore')
    guid = re.search(r'^\s*GUID\s+"?(' + GUID + ')"?', t, re.M).group(1)
    name = re.search(r'^\s*ID\s+"([^"]+)"', t, re.M).group(1)
    dm = re.search(r'Dependencies\s*\{(.*?)\}', t, re.S)
    deps = re.findall(GUID, dm.group(1)) if dm else []
    if kind is None:
        if name == 'ELifeRPG_Core': kind = 'core'
        elif name in TOP_NAMES: kind = 'top'
        else: kind = 'base'
    if guid in addons: problem(f'DUP ADDON GUID {guid}: {addons[guid]["name"]} and {name}')
    addons[guid] = dict(name=name, dir=gp.parent, kind=kind, deps=deps, owns={})

def rel(p): return p.relative_to(root)

# Resource ownership and stale .meta paths
owner = {}
for a in addons.values():
    for m in a['dir'].rglob('*.meta'):
        mm = re.search(r'Name "\{(' + GUID + r')\}([^"]*)"', m.read_text(errors='ignore'))
        if not mm: continue
        rg, path = mm.groups()
        if rg in owner: problem(f'DUP GUID {rg}: {owner[rg]} and {a["name"]}')
        owner[rg] = a['name']
        a['owns'][rg] = m
        real = m.relative_to(a['dir']).as_posix()[:-len('.meta')]
        if path != real: print(f'warning: STALE META PATH {rel(m)}: Name says {path}')

def closure(g, seen=None):
    seen = set() if seen is None else seen
    if g in seen or g not in addons: return seen
    seen.add(g)
    for d in addons[g]['deps']: closure(d, seen)
    return seen

# Dependency direction per kind
for g, a in addons.items():
    for d in a['deps']:
        if d not in addons: continue  # vanilla or third-party
        dk, dn = addons[d]['kind'], addons[d]['name']
        ok = {
            'core': dk == 'base',
            'base': False,
            'dlc': dk in ('core', 'base'),
            'top': True,
        }[a['kind']]
        if not ok: problem(f'WRONG DEPENDENCY {a["name"]} ({a["kind"]}) -> {dn} ({dk})')
    if a['kind'] == 'dlc' and not any(addons.get(d, {}).get('kind') == 'core' for d in a['deps']):
        problem(f'DLC {a["name"]} DOES NOT DEPEND ON ELifeRPG_Core')

# Logic in base-content addons, vanilla overrides outside Core
for a in addons.values():
    if a['kind'] == 'base':
        for f in a['dir'].rglob('*'):
            if f.is_file() and f.suffix in LOGIC_SUFFIXES: problem(f'LOGIC IN BASE-CONTENT ADDON {rel(f)}')
    if a['kind'] != 'core':
        for o in VANILLA_OVERRIDES:
            if (a['dir'] / o).exists(): problem(f'VANILLA OVERRIDE OUTSIDE CORE {rel(a["dir"] / o)}')

# References
for g, a in addons.items():
    visible = {addons[x]['name'] for x in closure(g)}
    for f in a['dir'].rglob('*'):
        if not f.is_file() or f.suffix in SKIP_SUFFIXES: continue
        for rg, path in set(ref_re.findall(f.read_bytes())):
            rg, p = rg.decode(), path.decode().strip()
            o = owner.get(rg)
            if o and o not in visible:
                problem(f'WRONG DIRECTION {a["name"]}: {rel(f)} -> {o} {{{rg}}}{p}')
            elif not o:
                hits = [x['name'] for x in addons.values() if p and (x['dir'] / p).exists()]
                if hits: problem(f'GUID MISMATCH {rel(f)} -> {{{rg}}}{p} (path in {hits})')

print('addons:', {f'{a["name"]} ({a["kind"]})': len(a['owns']) for a in addons.values()})
print('problems:', problems)
sys.exit(1 if problems else 0)
