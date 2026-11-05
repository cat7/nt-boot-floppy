#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 powermac-nt-hal contributors
"""mkpages.py --version V --out site — the GitHub Pages site: one page with the downloads in
site/download/ and the HOWTO, rendered plainly (no dependencies beyond Python)."""
import argparse, html, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def md_to_html(text):
    """Enough Markdown for docs/HOWTO.md: headings, paragraphs, fenced code, tables, lists,
    inline code, bold, links.  Not a general converter."""
    out, lines, i = [], text.splitlines(), 0
    def inline(s):
        s = html.escape(s)
        s = re.sub(r'`([^`]+)`', r'<code>\1</code>', s)
        s = re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', s)
        s = re.sub(r'\*([^*]+)\*', r'<i>\1</i>', s)
        s = re.sub(r'!\[([^\]]*)\]\(([^)]+)\)', r'<img alt="\1" src="\2">', s)
        s = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', s)
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
            out.append('<blockquote>' + md_to_html('\n'.join(q)) + '</blockquote>'); continue
        if not l.strip():
            i += 1; continue
        p = []
        while i < len(lines) and lines[i].strip() and not lines[i].startswith(('#', '```', '|', '>')) and not re.match(r'^\s*([-*]|\d+\.)\s', lines[i]):
            p.append(lines[i]); i += 1
        out.append('<p>' + inline(' '.join(p)) + '</p>')
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--version', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    dl = os.path.join(a.out, 'download')
    files = sorted(os.listdir(dl)) if os.path.isdir(dl) else []
    sums = {}
    if 'SHA256SUMS' in files:
        for line in open(os.path.join(dl, 'SHA256SUMS')):
            h, _, n = line.strip().partition('  ')
            sums[n] = h
    howto = open(os.path.join(ROOT, 'docs', 'HOWTO.md')).read().replace('../traces/', 'https://raw.githubusercontent.com/pappadf/powermac-nt-hal/main/traces/')
    rows = ''.join(f'<tr><td><a href="download/{html.escape(f)}">{html.escape(f)}</a></td>'
                   f'<td>{os.path.getsize(os.path.join(dl, f)):,} bytes</td><td><code>{sums.get(f, "")}</code></td></tr>'
                   for f in files)
    page = f'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>powermac-nt-hal {html.escape(a.version)}</title>
<style>
body{{max-width:52em;margin:2em auto;padding:0 1em;font:16px/1.5 system-ui,sans-serif;color:#222;background:#fff}}
code,pre{{font:14px/1.4 ui-monospace,monospace;background:#f3f3f3}} pre{{padding:.8em;overflow:auto}} code{{padding:0 .2em}}
table{{border-collapse:collapse;margin:1em 0}} td{{border:1px solid #ccc;padding:.3em .6em;vertical-align:top}}
blockquote{{border-left:4px solid #ccc;margin:1em 0;padding:0 1em;color:#444}} img{{max-width:100%}}
h1,h2,h3{{line-height:1.25}}
@media(prefers-color-scheme:dark){{body{{color:#ddd;background:#111}} code,pre{{background:#222}} td{{border-color:#444}}}}
</style></head><body>
<h1>powermac-nt-hal {html.escape(a.version)}</h1>
<p>Windows NT 4.0 for PowerPC on the Apple Network Server 500/700: a HAL, an ADB driver and an
ARC firmware, all built from source, on one boot floppy. <a href="https://github.com/pappadf/powermac-nt-hal">Source on GitHub.</a></p>
<h2>Download</h2>
<p>Nothing on the floppy is Microsoft's; it is GPL-2.0-only, like everything here. You still need
the Network Server's Open Firmware 2.26NT ROM and your own Windows NT 4.0 Workstation for PowerPC CD.</p>
<table>{rows}</table>
<hr>
{md_to_html(howto)}
</body></html>'''
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, 'index.html'), 'w') as f:
        f.write(page)
    print(f'{a.out}/index.html: {len(page)} bytes, {len(files)} downloads')


if __name__ == '__main__':
    main()
