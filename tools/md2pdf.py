#!/usr/bin/env python3
"""Render docs/benutzerhandbuch.md -> PDF via python-markdown + chromium."""
import subprocess
import sys
import pathlib

import markdown

ROOT = pathlib.Path(__file__).resolve().parent.parent
MD = ROOT / "docs" / "benutzerhandbuch.md"
HTML = pathlib.Path("/tmp/opencode/benutzerhandbuch.html")
PDF = ROOT / "docs" / "benutzerhandbuch.pdf"

with open(MD, "r", encoding="utf-8") as f:
    body = markdown.markdown(
        f.read(),
        extensions=["tables", "fenced_code", "sane_lists"],
        output_format="html5",
    )

# Make relative image references absolute so the temp HTML can load them.
doc_dir = (ROOT / "docs").as_uri()
body = body.replace('src="img/', f'src="{doc_dir}/img/')

css = """
:root { color-scheme: light; }
@page { size: A4; margin: 17mm 15mm 18mm 15mm; }
html { -webkit-print-color-adjust: exact; }
body {
  font-family: 'DejaVu Sans', 'Segoe UI', Arial, sans-serif;
  font-size: 10.2pt; line-height: 1.42; color: #1a1a1a; margin: 0;
}
h1 { font-size: 19pt; border-bottom: 2.5pt solid #0b6bcb; padding-bottom: 4pt;
     margin: 0 0 10pt 0; color: #0b3d6b; }
h1 + p, h1 + blockquote { margin-top: 2pt; }
h2 { font-size: 13.5pt; color: #0b3d6b; border-bottom: 1pt solid #b9d3ec;
     padding-bottom: 2pt; margin: 16pt 0 6pt 0; page-break-after: avoid; }
h3 { font-size: 11.5pt; color: #0b3d6b; margin: 11pt 0 4pt 0;
     page-break-after: avoid; }
h1 .h-sub, h2 .h-sub, h3 .h-sub, h4 .h-sub { display: block;
  font-size: 0.68em; font-weight: 400; margin-top: 2pt; }
p { margin: 4pt 0; }
ul, ol { margin: 4pt 0 4pt 0; padding-left: 16pt; }
li { margin: 1.5pt 0; }
blockquote { margin: 6pt 0; padding: 5pt 9pt; background: #eef5fb;
  border-left: 3pt solid #0b6bcb; border-radius: 0 3pt 3pt 0; }
blockquote p { margin: 0; }
code { font-family: 'DejaVu Sans Mono', Consolas, monospace; font-size: 8.8pt;
  background: #f0f0f0; padding: 0.5pt 2.5pt; border-radius: 2pt; }
pre { background: #f4f6f8; border: 1pt solid #d9dee3; border-radius: 3pt;
  padding: 7pt 9pt; overflow-wrap: break-word; white-space: pre-wrap; }
pre code { background: none; padding: 0; font-size: 9pt; }
table { border-collapse: collapse; width: 100%; margin: 6pt 0 8pt 0;
  font-size: 9.3pt; }
th, td { border: 0.7pt solid #c5cdd5; padding: 3pt 6pt; text-align: left;
  vertical-align: top; }
th { background: #e8f0f8; color: #0b3d6b; font-weight: bold; }
tr:nth-child(even) td { background: #f7f9fb; }
hr { border: none; border-top: 1pt solid #c5cdd5; margin: 12pt 0; }
strong { color: #0b3d6b; }
a { color: #0b6bcb; text-decoration: none; }
figure.portal-shot { margin: 8pt 0 10pt 0; page-break-inside: avoid; }
.portal-shot-ph { height: 130pt; border: 2pt dashed #a9b6c3;
  background: #f2f5f8; border-radius: 4pt; display: flex;
  align-items: center; justify-content: center; color: #7a8794;
  font-style: italic; font-size: 10pt; }
figure.portal-shot figcaption { font-size: 9pt; color: #5a6672;
  margin-top: 3pt; text-align: center; }
figure.ports-shot { margin: 10pt 0 12pt 0; page-break-inside: avoid;
  text-align: center; }
figure.ports-shot img { width: 90%; }
figure.ports-shot figcaption { font-size: 9pt; color: #5a6672;
  margin-top: 4pt; }
"""

html = f"""<!DOCTYPE html>
<html lang="de"><head><meta charset="utf-8">
<title>RCT Power Panel — Benutzerhandbuch</title>
<style>{css}</style></head>
<body>{body}</body></html>
"""

HTML.write_text(html, encoding="utf-8")

cmd = [
    "chromium", "--headless", "--disable-gpu", "--no-sandbox",
    "--no-pdf-header-footer", f"--print-to-pdf={PDF}", str(HTML),
]
r = subprocess.run(cmd, capture_output=True, text=True)
if r.returncode != 0:
    sys.exit(f"chromium failed: {r.stderr[-2000:]}")

print(f"PDF geschrieben: {PDF} ({PDF.stat().st_size} Bytes)")