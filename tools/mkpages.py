#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 powermac-nt-hal contributors
"""mkpages.py --versions DIR --out SITE [--repo OWNER/NAME] -- the GitHub Pages site, every
release on it, each for good.

DIR holds one directory per published release, named by its tag, each with `download/` (the
release's assets) and `HOWTO.md` (docs/HOWTO.md as of that tag), and optionally `meta.json`
({"prerelease": bool, "date": "YYYY-MM-DD"}).  release.yml fills it from the GitHub Releases
and the tags on every run, so the site is rebuilt whole from sources that persist; nothing
depends on what an earlier deployment left behind.

The site:

    index.html           the newest release's page, and the list of every release
    vX.Y/index.html      that release's page: its downloads and its HOWTO, for good
    vX.Y/download/...    that release's assets
    download/...         the newest release's assets, and every release's versioned files
                         (powermac-nt-hal-vX.Y-*), so a link to any of them keeps working

No dependencies beyond Python."""
import argparse, html, json, os, posixpath, re, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

def md_to_html(text, link=lambda u: u):
    """Enough Markdown for docs/HOWTO.md: headings, paragraphs, fenced code, tables, lists,
    inline code, bold, links.  Not a general converter."""
    out, lines, i = [], text.splitlines(), 0
    def inline(s):
        s = html.escape(s)
        s = re.sub(r'`([^`]+)`', r'<code>\1</code>', s)
        s = re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', s)
        s = re.sub(r'\*([^*]+)\*', r'<i>\1</i>', s)
        s = re.sub(r'!\[([^\]]*)\]\(([^)]+)\)', lambda m: f'<img alt="{m.group(1)}" src="{link(m.group(2), True)}">', s)
        s = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', lambda m: f'<a href="{link(m.group(2), False)}">{m.group(1)}</a>', s)
        return s
    while i < len(lines):
        l = lines[i]
        if l.startswith('<!--'):
            i += 1; continue
        if l.startswith('```'):
            j = i + 1
            while j < len(lines) and not lines[j].startswith('```'): j += 1
            out.append('<pre>' + html.escape('\n'.join(lines[i + 1:j])) + '</pre>'); i = j + 1; continue
        m = re.match(r'^(#{1,6})\s+(.*)', l)
        if m:
            out.append(f'<h{len(m.group(1))}>{inline(m.group(2))}</h{len(m.group(1))}>'); i += 1; continue
        if l.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].startswith('|'):
                cells = [c.strip() for c in lines[i].strip().strip('|').split('|')]
                if not all(re.match(r'^-+$', c) for c in cells if c): rows.append(cells)
                i += 1
            out.append('<table>' + ''.join('<tr>' + ''.join(f'<td>{inline(c)}</td>' for c in r) + '</tr>' for r in rows) + '</table>')
            continue
        if re.match(r'^\s*[-*]\s', l) or re.match(r'^\s*\d+\.\s', l):
            items = []
            while i < len(lines) and (re.match(r'^\s*[-*]\s', lines[i]) or re.match(r'^\s*\d+\.\s', lines[i]) or (lines[i].startswith('  ') and items)):
                if lines[i].startswith('  ') and not re.match(r'^\s*[-*\d]', lines[i]): items[-1] += ' ' + lines[i].strip()
                else: items.append(re.sub(r'^\s*([-*]|\d+\.)\s', '', lines[i]))
                i += 1
            out.append('<ul>' + ''.join(f'<li>{inline(x)}</li>' for x in items) + '</ul>'); continue
        if l.startswith('>'):
            q = []
            while i < len(lines) and lines[i].startswith('>'): q.append(lines[i][1:].strip()); i += 1
            out.append('<blockquote>' + md_to_html('\n'.join(q), link) + '</blockquote>'); continue
        if not l.strip():
            i += 1; continue
        p = []
        while i < len(lines) and lines[i].strip() and not lines[i].startswith(('#', '```', '|', '>')) and not re.match(r'^\s*([-*]|\d+\.)\s', lines[i]):
            p.append(lines[i]); i += 1
        out.append('<p>' + inline(' '.join(p)) + '</p>')
    return '\n'.join(out)



def version_key(tag):
    """v1.10 after v1.9; a tag that is not vN[.N...] sorts first."""
    m = re.match(r'^v(\d+(?:\.\d+)*)(.*)$', tag)
    if not m:
        return ((), tag)
    return (tuple(int(x) for x in m.group(1).split('.')), m.group(2))


def doc_link(repo, tag):
    """HOWTO.md's relative links point into the repository at that tag: images to the raw file,
    everything else to the file's page.  Absolute links and #anchors stay."""
    def link(u, image):
        if re.match(r'^[a-z][a-z0-9+.-]*:', u, re.I) or u.startswith('#'):
            return u
        path, _, frag = u.partition('#')
        path = posixpath.normpath(posixpath.join('docs', path))
        if image:
            return f'https://raw.githubusercontent.com/{repo}/{tag}/{path}'
        return f'https://github.com/{repo}/blob/{tag}/{path}' + (f'#{frag}' if frag else '')
    return link


STYLE = """
body{max-width:52em;margin:2em auto;padding:0 1em;font:16px/1.5 system-ui,sans-serif;color:#222;background:#fff}
code,pre{font:14px/1.4 ui-monospace,monospace;background:#f3f3f3} pre{padding:.8em;overflow:auto} code{padding:0 .2em}
table{border-collapse:collapse;margin:1em 0} td,th{border:1px solid #ccc;padding:.3em .6em;vertical-align:top;text-align:left}
blockquote{border-left:4px solid #ccc;margin:1em 0;padding:0 1em;color:#444} img{max-width:100%}
h1,h2,h3{line-height:1.25} nav{font-size:.9em;margin-bottom:1em} .note{background:#fff7d6;padding:.5em .8em}
@media(prefers-color-scheme:dark){body{color:#ddd;background:#111} code,pre{background:#222} td,th{border-color:#444}
.note{background:#332b00}}
"""


def downloads_table(dl, href_prefix):
    files = sorted(f for f in os.listdir(dl) if os.path.isfile(os.path.join(dl, f))) if os.path.isdir(dl) else []
    sums = {}
    if 'SHA256SUMS' in files:
        for line in open(os.path.join(dl, 'SHA256SUMS')):
            h, _, n = line.strip().partition('  ')
            sums[n] = h
    rows = ''.join(f'<tr><td><a href="{href_prefix}{html.escape(f)}">{html.escape(f)}</a></td>'
                   f'<td>{os.path.getsize(os.path.join(dl, f)):,} bytes</td><td><code>{sums.get(f, "")}</code></td></tr>'
                   for f in files)
    return f'<table>{rows}</table>', len(files)


def versions_table(releases, latest, prefix):
    rows = []
    for r in releases:
        tag = r['tag']
        what = 'newest' if tag == latest else ('pre-release' if r['prerelease'] else '')
        rows.append(f'<tr><td><a href="{prefix}{html.escape(tag)}/">{html.escape(tag)}</a></td>'
                    f'<td>{html.escape(r["date"])}</td><td>{what}</td></tr>')
    return '<table><tr><th>Release</th><th>Date</th><th></th></tr>' + ''.join(rows) + '</table>'


def page(title, body):
    return (f'<!doctype html>\n<html lang="en"><head><meta charset="utf-8">'
            f'<meta name="viewport" content="width=device-width, initial-scale=1">'
            f'<title>{html.escape(title)}</title><style>{STYLE}</style></head><body>\n{body}\n</body></html>\n')


INTRO = ('<p>Windows NT 4.0 for PowerPC on the Apple Network Server 500/700: a HAL, an ADB driver and '
         'an ARC firmware, all built from source, on one boot floppy. '
         '<a href="https://github.com/{repo}">Source on GitHub.</a></p>')
DL_NOTE = ('<p>Nothing on the floppy is Microsoft\'s; it is GPL-2.0-only, like everything here. '
           'What else you need (a ROM, your own NT CD) is in <i>What you need</i> below.</p>')


def release_body(r, repo, dl_table, nav, banner=''):
    howto = open(r['howto']).read()
    return (f'{nav}<h1>powermac-nt-hal {html.escape(r["tag"])}</h1>\n{banner}'
            + INTRO.format(repo=html.escape(repo))
            + f'\n<h2>Download</h2>\n{DL_NOTE}\n{dl_table}\n<hr>\n'
            + md_to_html(howto, doc_link(repo, r['tag'])))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--versions', required=True, help='one directory per release tag: download/, HOWTO.md, meta.json')
    ap.add_argument('--out', required=True)
    ap.add_argument('--repo', default=os.environ.get('GITHUB_REPOSITORY', 'pappadf/powermac-nt-hal'))
    a = ap.parse_args()

    releases = []
    for tag in os.listdir(a.versions):
        d = os.path.join(a.versions, tag)
        if not os.path.isdir(d) or not os.path.isfile(os.path.join(d, 'HOWTO.md')):
            continue
        meta = {}
        if os.path.isfile(os.path.join(d, 'meta.json')):
            meta = json.load(open(os.path.join(d, 'meta.json')))
        releases.append({'tag': tag, 'dir': d, 'howto': os.path.join(d, 'HOWTO.md'),
                         'prerelease': bool(meta.get('prerelease')), 'date': meta.get('date', '')})
    if not releases:
        raise SystemExit(f'{a.versions}: no releases (each needs a HOWTO.md)')
    releases.sort(key=lambda r: version_key(r['tag']), reverse=True)
    finals = [r for r in releases if not r['prerelease']]
    latest = (finals or releases)[0]['tag']

    if os.path.exists(a.out):
        shutil.rmtree(a.out)
    os.makedirs(os.path.join(a.out, 'download'))
    # every release's own page and files
    for r in releases:
        vd = os.path.join(a.out, r['tag'])
        shutil.copytree(os.path.join(r['dir'], 'download'), os.path.join(vd, 'download'))
        table, n = downloads_table(os.path.join(vd, 'download'), 'download/')
        nav = f'<nav><a href="../">All releases</a></nav>'
        banner = ('' if r['tag'] == latest else
                  f'<p class="note">This is an older release. The newest is <a href="../{html.escape(latest)}/">{html.escape(latest)}</a>.</p>\n')
        with open(os.path.join(vd, 'index.html'), 'w') as f:
            f.write(page(f'powermac-nt-hal {r["tag"]}', release_body(r, a.repo, table, nav, banner)))
        print(f'{vd}/index.html: {n} downloads')
        # versioned files also at the top-level download/, where the first release put them
        for fn in os.listdir(os.path.join(vd, 'download')):
            if fn.startswith(f'powermac-nt-hal-{r["tag"]}-'):
                shutil.copy(os.path.join(vd, 'download', fn), os.path.join(a.out, 'download', fn))
    # the newest release again at the root, with its plain files (SHA256SUMS, LICENSE, ...)
    newest = next(r for r in releases if r['tag'] == latest)
    for fn in os.listdir(os.path.join(newest['dir'], 'download')):
        shutil.copy(os.path.join(newest['dir'], 'download', fn), os.path.join(a.out, 'download', fn))
    table, n = downloads_table(os.path.join(a.out, latest, 'download'), f'{latest}/download/')
    nav = '<h2>Releases</h2>\n' + versions_table(releases, latest, '') + '\n'
    body = release_body(newest, a.repo, table, '')
    body = body.replace('<hr>\n', '<hr>\n' + nav + '<hr>\n', 1)
    with open(os.path.join(a.out, 'index.html'), 'w') as f:
        f.write(page(f'powermac-nt-hal {latest}', body))
    with open(os.path.join(a.out, '.nojekyll'), 'w'):
        pass
    print(f'{a.out}/index.html: {latest} is the newest of {len(releases)} release(s)')


if __name__ == '__main__':
    main()
