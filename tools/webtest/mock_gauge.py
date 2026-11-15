#!/usr/bin/env python3
"""Pretend to be the gauge's web server so the settings page can be exercised in a
browser (run_page_test.py). Serves the REAL page text from oil_page.h, and answers
every settings / live / presets / zero request with the REAL C++ logic
(oil_settings.h, oil_math.h, oil_api.h) through gauge_core, so the page is tested
against the gauge's actual rules rather than a copy of them."""
import json, re, subprocess, sys, threading, time, math
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

HERE = __file__.rsplit('/', 1)[0]
html = open(HERE + '/../../oil_page.h').read()
PAGE = re.search(r'R"HTML\((.*)\)HTML"', html, re.S).group(1)
VERSION = re.search(r'#define OG_VERSION "([^"]+)"', open(HERE + '/../../oil_config.h').read()).group(1)

core = subprocess.Popen([HERE + '/gauge_core'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
lock = threading.Lock()
def ask(*parts):
    with lock:
        core.stdin.write('\t'.join(str(p) for p in parts) + '\n'); core.stdin.flush()
        return core.stdout.readline().strip()

# Designed screens. The real gauge validates with the layout engine (Gauge_Layout.cpp, tested natively
# elsewhere); here a layout is accepted when it is JSON in the right format, and a view can be a screen when it
# is custom, switched on, and offered to screen 1 - the same rules OilLayout_ViewUsable applies.
LAY = dict(json=None, error='')
LAY_MAX = 65520
def lay_views():
    if not LAY['json']: return []
    out = []
    for i, v in enumerate(LAY['json'].get('views', [])):
        if v.get('builtin'): continue
        scr = v.get('screens')
        usable = v.get('enabled', True) and (not scr or 1 in scr)
        out.append(dict(i=i, code=0x40 + i, name=v.get('name', v.get('id')), usable=bool(usable)))
    return out
def lay_sync():
    mask = 0
    for v in lay_views():
        if v['usable']: mask |= 1 << v['i']
    ask('views', mask)

STATE = dict(kind=1, o=2.5, b=1.6228, a=2.35, ssid='OilGauge-1A2B', log=[], ota=None, armed=True, demo_end=0.0)
DEMO_S = 120
def demo_left():
    return max(0, int(math.ceil(STATE['demo_end'] - time.time())))

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, body, ctype='application/json'):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code); self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        u = urlparse(self.path); q = {k: v[0] for k, v in parse_qs(u.query).items()}
        if u.path == '/': return self.send(200, PAGE, 'text/html')
        if u.path == '/api/live':
            name = {0: 'NO SENSOR', 1: 'M5 METER  0x49', 2: 'INA3221  0x40  O1 B2 A3', 3: 'ADS1115  0x48  O1 B2 A4', 4: 'PAHUB  0x70  O2 B1 A6', 5: 'PBHUB  0x61  O2 B1 A6'}[STATE['kind']]
            return self.send(200, ask('live', STATE['o'], STATE['b'], STATE['a'], STATE['kind'], 1 if STATE['armed'] else 0, name, demo_left()))
        if u.path == '/api/info':
            return self.send(200, json.dumps(dict(version=VERSION, build='Oct 14 2026 12:00:00', ssid=STATE['ssid'], defaultPass=(STATE['ssid'] == 'OilGauge-1A2B'), freeHeap=90000)))
        if u.path == '/api/layout':
            return self.send(200, json.dumps(dict(available=STATE.get('lay_avail', True), loaded=LAY['json'] is not None, bytes=len(json.dumps(LAY['json'])) if LAY['json'] else 0,
                                                  maxBytes=LAY_MAX, error=LAY['error'], views=lay_views())))
        if u.path == '/api/layout.json':
            return self.send(200 if LAY['json'] else 404, json.dumps(LAY['json']) if LAY['json'] else '{"ok":false,"error":"no layout stored"}')
        if u.path == '/api/settings': return self.send(200, ask('settings'))
        if u.path == '/api/presets':  return self.send(200, ask('presets'))
        if u.path == '/_state':
            return self.send(200, json.dumps(dict(S=json.loads(ask('settings')), flags=json.loads(ask('flags')), **STATE)))
        if u.path == '/_set':                                  # the test moves the simulated sensor voltages
            for k in ('o', 'b', 'a'):
                if k in q: STATE[k] = q[k]
            if 'kind' in q: STATE['kind'] = int(q['kind'])
            if 'lay_avail' in q: STATE['lay_avail'] = q['lay_avail'] == '1'
            if 'demo_end' in q: STATE['demo_end'] = float(q['demo_end'])
            return self.send(200, '{}')
        self.send(404, 'nf', 'text/plain')
    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0))
        if self.path == '/update':
            body = self.rfile.read(n)
            fname = re.search(rb'filename="([^"]*)"', body)
            STATE['ota'] = dict(bytes=n, name=fname.group(1).decode() if fname else None)
            if fname and b'merged' in fname.group(1):
                return self.send(500, json.dumps(dict(ok=False, error='Update failed: that is the full-flash image - use OilGauge370Z.ino.bin')))
            return self.send(200, json.dumps(dict(ok=True)))
        if self.path == '/api/layout':
            body = self.rfile.read(n)
            m = re.search(rb'\r\n\r\n(.*)\r\n--[^\r\n]*--\r\n?$', body, re.S)
            data = m.group(1) if m else b''
            def bad(msg): return self.send(400, json.dumps(dict(ok=False, error='Layout rejected: ' + msg)))
            if len(data) > LAY_MAX: return bad('file too large (limit %d bytes)' % LAY_MAX)
            try: j = json.loads(data)
            except Exception as e: return bad('not valid JSON')
            if j.get('format') != '370z-gauge-layout': return bad('not a 370z gauge layout file')
            if j.get('version') != 1: return bad('layout version %s not supported (this screen reads version 1)' % j.get('version'))
            LAY['json'] = j; LAY['error'] = ''; lay_sync(); STATE['log'].append(('layout', {}))
            return self.send(200, json.dumps(dict(ok=True, saved=True)))
        if self.path == '/api/layout/clear':
            self.rfile.read(n); LAY['json'] = None; lay_sync(); STATE['log'].append(('layout_clear', {}))
            return self.send(200, json.dumps(dict(ok=True)))
        form = {k: v[0] for k, v in parse_qs(self.rfile.read(n).decode(), keep_blank_values=True).items()}
        if self.path == '/api/settings':
            parts = ['post']
            for k, v in form.items(): parts += [k, v]
            r = ask(*parts); STATE['log'].append(('settings', form))
            return self.send(200 if json.loads(r).get('ok') else 400, r)
        if self.path == '/api/demo':
            r = ask('demoparse', form.get('on', ''))
            j = json.loads(r)
            if not j.get('ok'): return self.send(400, r)
            STATE['demo_end'] = time.time() + DEMO_S if j['on'] == 1 else 0.0
            STATE['log'].append(('demo', form))
            return self.send(200, json.dumps(dict(ok=True, demo=DEMO_S if j['on'] == 1 else 0)))
        if self.path == '/api/defaults':
            STATE['log'].append(('defaults', {})); return self.send(200, ask('defaults'))
        if self.path == '/api/zero':
            r = ask('zero', STATE['b'], STATE['kind']); STATE['log'].append(('zero', {}))
            return self.send(200 if json.loads(r).get('ok') else 400, r)
        if self.path == '/api/wifi':
            if len(form.get('pass', '')) not in (0,) and not (8 <= len(form['pass']) <= 63):
                return self.send(400, json.dumps(dict(ok=False, error='password must be 8 to 63 characters')))
            STATE['ssid'] = form['ssid']; STATE['log'].append(('wifi', form))
            return self.send(200, json.dumps(dict(ok=True, msg="Saved. The gauge restarts its Wi-Fi in a few seconds - rejoin '%s' with the new password." % form['ssid'])))
        self.send(404, 'nf', 'text/plain')

if __name__ == '__main__':
    ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1]) if len(sys.argv) > 1 else 8099), H).serve_forever()
