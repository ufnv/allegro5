#!/usr/bin/env python3
"""Point ICBM's Visual Studio projects at its own copy of Allegro + deps:

    <ICBM>/allegro5/include        Allegro headers
    <ICBM>/allegro5/lib            Allegro static libraries
    <ICBM>/allegro5/deps/include   third-party headers (zlib, png, ogg, theora...)
    <ICBM>/allegro5/deps/lib       third-party static libraries

    python set_icbm_allegro_paths.py <ICBM dir> [--dry-run]

For every project in <ICBM>/ICBMmber.sln (plus e1p and LPNG):
  * Header search paths - AdditionalIncludeDirectories, IncludePath,
    ExternalIncludePath: every Allegro header path (C:/Dev/allegro/include,
    .../allegro5/include, .../build/include, .../addons/<x>) is replaced by ONE
    entry for allegro5/include; every Allegro deps header path
    (.../allegro5/deps/include, C:/Dev/allegro/allegro_deps/include,
    allegro_winpkg .../allegro_deps/include) by ONE entry for allegro5/deps/include.
  * Library search paths (not in Win32 groups) - LibraryPath and
    AdditionalLibraryDirectories: Allegro build lib folders -> allegro5/lib,
    deps lib folders -> allegro5/deps/lib. Every <Link> that links Allegro gets
    allegro5/lib and allegro5/deps/lib as its FIRST two
    AdditionalLibraryDirectories entries (ahead of $(OutDir)).
All paths are written relative to the project. Line endings, BOM and the rest
of the file are preserved. Safe to re-run.
"""
import os
import re
import sys

EXTRA = ['e1p/E1P.vcxproj', 'LPNG/LPNG/lpng.vcxproj']
HEADER_TAGS = ('AdditionalIncludeDirectories', 'IncludePath', 'ExternalIncludePath')


def norm(entry):
    return entry.strip().strip('"').replace('\\', '/').lower().rstrip('/')


def is_deps(e):
    return 'allegro' in e and 'deps' in e


def is_allegro_header_dir(entry):
    e = norm(entry)
    return 'allegro' in e and not is_deps(e) and (e.endswith('/include') or '/addons/' in e)


def is_deps_header_dir(entry):
    e = norm(entry)
    return is_deps(e) and e.endswith('/include')


def is_allegro_lib_dir(entry):
    e = norm(entry)
    return ('allegro' in e and not is_deps(e) and '/lib' in e
            and ('/build' in e or 'allegro5.1-vc' in e))


def is_deps_lib_dir(entry):
    e = norm(entry)
    return is_deps(e) and e.endswith('/lib')


def rewrite_list(value, rules, force_first=()):
    """rules: [(matcher, new_entry)]. Each matching entry is replaced by its
    rule's new_entry (once, at the first match). Entries in force_first are
    moved/added to the front, in that order."""
    out, placed = [], set()
    news = {n for _, n in rules} | set(force_first)
    for e in value.split(';'):
        rule = next((n for match, n in rules if match(e)), None)
        if rule is None and e.strip() in news:
            rule = e.strip()
        if rule is None:
            out.append(e)
        elif rule not in placed and rule not in force_first:
            out.append(rule)
            placed.add(rule)
    if force_first:
        out = list(force_first) + [e for e in out if e.strip()]
    return ';'.join(out)


def fix_group(body, platform, p, nl):
    header_rules = [(is_deps_header_dir, p['deps_inc']), (is_allegro_header_dir, p['inc'])]

    def header(m):
        return m.group(1) + rewrite_list(m.group(2), header_rules) + m.group(3)
    for tag in HEADER_TAGS:
        body = re.sub(r'(<%s>)(.*?)(</%s>)' % (tag, tag), header, body, flags=re.S)

    if platform == 'Win32':
        return body

    lib_rules = [(is_deps_lib_dir, p['deps_lib']), (is_allegro_lib_dir, p['lib'])]

    def lib(m):
        return m.group(1) + rewrite_list(m.group(2), lib_rules) + m.group(3)
    body = re.sub(r'(<LibraryPath>)(.*?)(</LibraryPath>)', lib, body, flags=re.S)

    def link_block(m):
        block = m.group(0)
        deps = re.search(r'<AdditionalDependencies>(.*?)</AdditionalDependencies>', block, re.S)
        if not deps or not re.search(r'allegro[^;]*\.lib', deps.group(1), re.I):
            return block
        first = (p['lib'], p['deps_lib'])
        ld = re.search(r'(<AdditionalLibraryDirectories>)(.*?)(</AdditionalLibraryDirectories>)', block, re.S)
        if ld:
            val = rewrite_list(ld.group(2), lib_rules, force_first=first)
            return block[:ld.start()] + ld.group(1) + val + ld.group(3) + block[ld.end():]
        indent = re.search(r'\n([ \t]*)<AdditionalDependencies>', block)
        ind = indent.group(1) if indent else '      '
        tag = ('<AdditionalLibraryDirectories>%s;%%(AdditionalLibraryDirectories)'
               '</AdditionalLibraryDirectories>' % ';'.join(first))
        return block.replace('<Link>', '<Link>' + nl + ind + tag, 1)
    return re.sub(r'<Link>.*?</Link>', link_block, body, flags=re.S)


def fix_project(text, paths, nl):
    def group(m):
        # Condition="'$(Configuration)|$(Platform)'=='Release|x64'" -> x64
        cond = re.search(r"==\s*'[^'|]*\|([^']+)'", m.group(2))
        platform = cond.group(1) if cond else None
        return m.group(1) + m.group(2) + '>' + fix_group(m.group(3), platform, paths, nl) + m.group(4)
    # [^>]*(?<!/)> : skip self-closing <PropertyGroup Label="UserMacros" />
    return re.sub(r'(<(?:PropertyGroup|ItemDefinitionGroup))([^>]*)(?<!/)>(.*?)(</(?:PropertyGroup|ItemDefinitionGroup)>)',
                  group, text, flags=re.S)


def project_paths(icbm, project_dir):
    base = os.path.join(icbm, 'allegro5')
    rel = lambda *p: os.path.relpath(os.path.join(base, *p), project_dir)
    return {'inc': rel('include'), 'lib': rel('lib'),
            'deps_inc': rel('deps', 'include'), 'deps_lib': rel('deps', 'lib')}


def main():
    args = sys.argv[1:]
    dry = '--dry-run' in args
    if dry:
        args.remove('--dry-run')
    if len(args) != 1:
        sys.exit(__doc__)
    icbm = os.path.abspath(args[0])
    sln = open(os.path.join(icbm, 'ICBMmber.sln'), encoding='utf-8-sig').read()
    projects = re.findall(r'"([^"]+\.vcxproj)"', sln) + EXTRA

    for rel in projects:
        path = os.path.join(icbm, rel.replace('\\', os.sep).replace('/', os.sep))
        if not os.path.isfile(path):
            print('  missing  %s' % rel)
            continue
        raw = open(path, 'rb').read()
        bom = raw.startswith(b'\xef\xbb\xbf')
        text = raw.decode('utf-8-sig')
        nl = '\r\n' if '\r\n' in text else '\n'
        new = fix_project(text, project_paths(icbm, os.path.dirname(path)), nl)
        if new == text:
            print('  same     %s' % rel)
            continue
        print('  updated  %s' % rel)
        if not dry:
            open(path, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + new.encode('utf-8'))


if __name__ == '__main__':
    main()
