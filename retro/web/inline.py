#!/usr/bin/env python3
"""Fold the page, its script and the wasm module into one self-contained index.html (runs from file://, one file to host)."""
import base64, sys
page, js, wasm, out = sys.argv[1:5]
b64 = base64.b64encode(open(wasm, 'rb').read()).decode()
script = 'const WASM_B64="%s";\n%s' % (b64, open(js).read())
html = open(page).read().replace('<!--SCRIPT-->', '<script>\n%s\n</script>' % script)
open(out, 'w').write(html)
print('%s: %d bytes (wasm %d)' % (out, len(html), len(b64) * 3 // 4))
