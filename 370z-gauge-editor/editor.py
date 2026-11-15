#!/usr/bin/env python3
"""370Z layout editor - local server. One editor for both gauges: the standalone oil
gauge and the full CAN-bus screens (switch at the top of the page).

    python3 editor.py            (opens the editor in your browser)
    python3 editor.py --no-browser --port 8370
    python3 editor.py --edition can   (start on the full CAN-bus screens)
    python3 editor.py --lan       (on the build server: open it from other
                                   computers at http://<server>:8370)

Everything stays on this computer. The server only:
  * serves the editor page (web/),
  * opens / saves layout files in your layouts folder,
  * exports Gauge_Layouts.h into the screen sketch folder - on this computer,
    or on the build server over SSH ("user@server:~/path/to/LVGL_Arduino"),
  * fetches live data from the brain for the preview (read-only, the same
    JSON the screens read).
Settings are kept in ~/.config/370z-layout-editor/settings.json.
"""
import argparse
import base64, json, os, re, shlex, shutil, subprocess, sys, threading, time, urllib.request, webbrowser
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.join(HERE, "web")
CFG_DIR = os.path.join(os.path.expanduser("~"), ".config", "370z-layout-editor")
CFG = os.path.join(CFG_DIR, "settings.json")
EDITOR_VERSION = "1.4.2"
DEFAULT_EDITION = "gauge"                            # which version the page opens on (--edition)
STYLES = os.path.join(CFG_DIR, "styles.json")       # the favourite looks (view colours etc.)
# Old Gauge_Layouts.h files are kept here, NOT in the sketch: the sketch is in
# git and release.sh does "git add .", so a .bak there would get committed.
BACKUPS = os.path.join(os.path.expanduser("~"), ".local", "share", "370z-layout-editor", "backups")
DEFAULTS = {
    "layouts_dir": os.path.join(os.path.expanduser("~"), "370z-layouts"),
    "sketch_dir": os.path.join(os.path.expanduser("~"), "waveshare2.1", "Arduino", "examples", "LVGL_Arduino"),
    "brain_url": "http://10.42.0.1:8080",
}
TYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript", ".css": "text/css",
         ".wasm": "application/wasm", ".json": "application/json", ".svg": "image/svg+xml", ".png": "image/png"}
# Talk to the brain / ourselves directly, never through a desktop proxy setting.
OPEN = urllib.request.build_opener(urllib.request.ProxyHandler({})).open
NAME_OK = re.compile(r"^[A-Za-z0-9 _.\-()]{1,80}\.json$")


def load_settings():
    s = dict(DEFAULTS)
    try:
        with open(CFG, encoding="utf-8") as f:
            s.update({k: v for k, v in json.load(f).items() if k in DEFAULTS and isinstance(v, str)})
    except (OSError, ValueError):
        pass
    return s


def save_settings(s):
    os.makedirs(CFG_DIR, exist_ok=True)
    with open(CFG, "w", encoding="utf-8") as f:
        json.dump(s, f, indent=1)


def check_layout(data):
    if not isinstance(data, dict) or data.get("format") != "370z-gauge-layout" or data.get("version") != 1:
        raise ValueError("not a version 1 370z gauge layout file")


IMG_NAME = re.compile(r"^[a-z0-9_]{1,24}$")
BUILTIN_IMAGES = {"car_370z"}                       # in the screen firmware already (car_370z.c)


def image_arrays(data):
    """C arrays for the uploaded images the layout's image gauges use. Each is
    LVGL's TRUE_COLOR_ALPHA format: per pixel RGB565 (low byte first) + alpha."""
    images = data.get("images") or {}
    used = []
    for v in data.get("views", []):
        for g in v.get("gauges", []) or []:
            n = g.get("image") if g.get("type") == "image" else None
            if n and n not in BUILTIN_IMAGES and n not in used:
                used.append(n)
    out, table = [], []
    for n in used:
        im = images.get(n)
        if not isinstance(im, dict):
            raise ValueError('image "%s" is used by a gauge but is not in the layout file - upload it again' % n)
        if not IMG_NAME.match(n):
            raise ValueError('image name "%s": 1-24 of a-z 0-9 _' % n)
        w, h = int(im.get("w", 0)), int(im.get("h", 0))
        raw = base64.b64decode(im.get("data", ""))
        if not (1 <= w <= 480 and 1 <= h <= 480) or len(raw) != w * h * 3:
            raise ValueError('image "%s": bad size or data' % n)
        c = "gl_img_" + n
        hexs = ",".join("0x%02x" % b for b in raw)
        lines = [hexs[i:i + 120] for i in range(0, len(hexs), 120)]   # 24 bytes a line
        out.append('// GLIMG "%s" %d %d\n'
                   'static const uint8_t %s_map[] = {\n%s\n};\n'
                   "static const lv_img_dsc_t %s = {{LV_IMG_CF_TRUE_COLOR_ALPHA, 0, 0, %d, %d}, %d, %s_map};\n"
                   % (n, w, h, c, "\n".join(lines), c, w, h, len(raw), c))
        table.append('{"%s", &%s}' % (n, c))
    if not table:
        return ""
    return ("\n// Images the layouts use (uploaded in the editor).\n" + "".join(out) +
            "#define GL_HAS_LAYOUT_IMAGES 1\n"
            "static const int GL_LAYOUT_IMAGE_COUNT = %d;\n"
            "static const GlImage GL_LAYOUT_IMAGES[] = {%s};\n" % (len(table), ", ".join(table)))


def images_from_header(t):
    """The images written by image_arrays(), back into layout-file form."""
    out = {}
    for m in re.finditer(r'// GLIMG "([a-z0-9_]+)" (\d+) (\d+)\r?\nstatic const uint8_t \w+_map\[\] = \{(.*?)\};', t, re.S):
        raw = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(4)))
        out[m.group(1)] = {"w": int(m.group(2)), "h": int(m.group(3)), "data": base64.b64encode(raw).decode()}
    return out


def header_text(data):
    """Gauge_Layouts.h, the same as tools/export_layouts.py writes (CRLF on disk).
    Uploaded images go in as C arrays, not inside the layout text."""
    body = {k: v for k, v in data.items() if k != "images"}
    text = json.dumps(body, indent=1, ensure_ascii=False)
    if ')GLJSON"' in text:
        raise ValueError("layout text contains the end marker )GLJSON\" - rename something")
    names = ", ".join(v.get("name", v.get("id", "?")) for v in data.get("views", []))
    return ("// Gauge_Layouts.h - custom views built into the screen firmware.\n"
            "// GENERATED by the layout editor (or tools/export_layouts.py) - edit the\n"
            "// layouts in the editor, not here. Views: %s\n"
            "#pragma once\n"
            'static const char GL_DEFAULT_LAYOUTS[] = R"GLJSON(%s)GLJSON";\n') % (names or "none", text) + image_arrays(data)


# ------------------------------------------------------------ sketch folder ---
# Either a folder here, or "user@host:/path" / "host:~/path" on the build
# server, reached with the system ssh (key login: no password prompts here).
REMOTE = re.compile(r"^([^/\s:]+):(.+)$")


def remote_of(d):
    m = REMOTE.match(d.strip())
    return (m.group(1), m.group(2).strip()) if m else None


def rpath(p, name=""):
    """Shell expression for a remote path; keeps ~ working."""
    p = p.rstrip("/") + ("/" + name if name else "")
    if p == "~" or p.startswith("~/"):
        return '"$HOME"' + (shlex.quote(p[1:]) if len(p) > 1 else "")
    return shlex.quote(p)


def ssh(host, cmd, data=None, timeout=20):
    try:
        r = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", host, cmd],
                           input=data, capture_output=True, timeout=timeout)
    except FileNotFoundError:
        raise OSError("ssh isn't installed on this computer (sudo apt install openssh-client)")
    except subprocess.TimeoutExpired:
        raise OSError("%s didn't answer in time - is the server on and reachable?" % host)
    if r.returncode == 255:
        msg = r.stderr.decode("utf-8", "replace").strip().splitlines()
        msg = msg[-1] if msg else "connection failed"
        if "Permission denied" in msg:
            msg += " - this computer needs key login to %s: run  ssh-copy-id %s  once in a terminal" % (host, host)
        elif "Host key verification failed" in msg:
            msg += " - connect once in a terminal first (ssh %s) and answer yes" % host
        raise OSError("ssh %s: %s" % (host, msg))
    return r


def sketch_check(d):
    rm = remote_of(d)
    if not rm:
        ok = os.path.isfile(os.path.join(d, "Gauge_Layout.cpp"))
        return ok, ("Gauge_Layout.cpp found there" if ok else "no Gauge_Layout.cpp in %s" % d)
    host, p = rm
    try:
        r = ssh(host, "test -f %s" % rpath(p, "Gauge_Layout.cpp"))
    except OSError as e:
        return False, str(e)
    return (r.returncode == 0,
            "connected to %s, Gauge_Layout.cpp found there" % host if r.returncode == 0 else
            "connected to %s, but there's no Gauge_Layout.cpp in %s" % (host, p))


def sketch_read(d, name="Gauge_Layouts.h"):
    rm = remote_of(d)
    if not rm:
        with open(os.path.join(d, name), encoding="utf-8", newline="") as f:   # keep CRLF
            return f.read()
    host, p = rm
    r = ssh(host, "cat %s" % rpath(p, name))
    if r.returncode:
        raise OSError("no %s in %s on %s" % (name, p, host))
    return r.stdout.decode("utf-8", "replace")


def builtin_views(d):
    """The screen code's built-in views, read from Gauge_Telemetry.h in the sketch:
    the GA_VIEW_LIST X-macro, or (older code) the plain enum View. Also whether
    the sketch's layout engine understands built-in entries in a layout file."""
    t = sketch_read(d, "Gauge_Telemetry.h").replace("\r\n", "\n")
    views = [{"key": m.group(1).lower(), "name": m.group(2)}
             for m in re.finditer(r'^\s*X\(\s*([A-Z0-9_]+)\s*,\s*"([^"]*)"\s*\)', t, re.M)]
    source = "GA_VIEW_LIST"
    if not views:
        m = re.search(r"enum\s+View\s*\{(.*?)\}", t, re.S)
        if m:
            for line in m.group(1).split("\n"):
                mm = re.match(r"\s*VIEW_([A-Z0-9_]+)\s*(?:=\s*\d+\s*)?,?\s*(?://\s*(.*))?$", line)
                if mm and mm.group(1) != "COUNT":
                    views.append({"key": mm.group(1).lower(), "name": (mm.group(2) or mm.group(1).title()).strip()})
        source = "enum View"
    try:
        lh = sketch_read(d, "Gauge_Layout.h")
    except OSError:
        lh = ""
    supported = "GL_ViewBuiltin" in lh
    return {"ok": bool(views), "views": views, "source": source, "supported": supported,
            "bg": "GL_ViewBg" in lh,                   # views can set their own background colour
            "media": "GL_SetImages" in lh,             # clock and image gauges
            "scalecol": engine_has(d, "minor_ticks"),  # colours for scale numbers, ticks and unit
            "lamp": engine_has(d, "seg7Mask"),         # glow, seven-segment digits, steps from zero, alert zone colours, fixed-width numbers
            # the layout engine can redraw built-in views (notes, marks, by_unit scales, lines...)
            "convert": supported and os.path.isfile(os.path.join(WEB, "convert.js")) and convert_ok(d),
            "config": screen_limits(d),
            "error": "" if views else "no built-in view list found in Gauge_Telemetry.h"}


# The ranges and alert limits the built-in views use, for converting them into
# editable views. Only these names are read - nothing else in Gauge_Config.h
# (Wi-Fi details) ever leaves the file.
LIMIT_NAMES = ["GA_OIL_MAX", "GA_OIL_MAX_BAR", "GA_OIL_WARN_PSI", "GA_OIL_CRIT_PSI",
               "GA_OILT_MIN_C", "GA_OILT_MAX", "GA_OILT_MIN_F", "GA_OILT_MAX_F", "GA_OILT_WARN_C", "GA_OILT_CRIT_C",
               "GA_VOLT_MIN", "GA_VOLT_MAX", "GA_VOLT_CRIT", "GA_VOLT_LOW", "GA_VOLT_HIGH", "GA_SPD_MAX",
               "GA_AFR_MIN", "GA_AFR_MAX", "GA_AFR_STOICH", "GA_AFR_LOAD_PCT", "GA_AFR_LEAN_WARN", "GA_AFR_LEAN_CRIT",
               "GA_AFR_RICH_INFO", "GA_BOOST_MIN", "GA_BOOST_MAX", "GA_BOOST_WARN", "GA_BOOST_CRIT",
               "GA_COOL_MIN_C", "GA_COOL_MAX_C", "GA_COOL_MIN_F", "GA_COOL_MAX_F", "GA_COOL_WARN_C", "GA_COOL_CRIT_C",
               "GA_TYRE_WARN_LO", "GA_TYRE_CRIT_LO", "GA_TYRE_WARN_HI", "GA_TYRE_CRIT_HI"]
DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Z_][A-Z0-9_]*)[ \t]+([^\n]*)$", re.M)


def _calc(expr, defs, depth=0):
    """Value of a simple #define: numbers (1.4f, 20), + - * / and brackets, and
    other #defines. Anything else (strings, casts, function calls) -> None."""
    import ast
    expr = re.sub(r"/\*.*?\*/|//.*$", "", expr).strip()
    if not expr or depth > 8:
        return None
    expr = re.sub(r"\b(\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?[fFuUlL]*\b",
                  lambda m: re.sub(r"[fFuUlL]+$", "", m.group(0)), expr)
    def name(m):
        v = defs.get(m.group(0))
        if v is None:
            raise ValueError
        val = _calc(v, defs, depth + 1)
        if val is None:
            raise ValueError
        return "(%r)" % val
    try:
        expr = re.sub(r"\b[A-Za-z_][A-Za-z0-9_]*\b", name, expr)
        tree = ast.parse(expr, mode="eval")
    except (ValueError, SyntaxError):
        return None
    ok = (ast.Expression, ast.BinOp, ast.UnaryOp, ast.Constant, ast.Add, ast.Sub, ast.Mult, ast.Div, ast.USub, ast.UAdd)
    if not all(isinstance(n, ok) for n in ast.walk(tree)) or not all(
            isinstance(n.value, (int, float)) for n in ast.walk(tree) if isinstance(n, ast.Constant)):
        return None
    try:
        return float(eval(compile(tree, "<define>", "eval"), {"__builtins__": {}}, {}))
    except (ZeroDivisionError, OverflowError):
        return None


def sketch_headers(d):
    """Names of the .h files in the sketch folder (here or on the build server)."""
    rm = remote_of(d)
    try:
        if not rm:
            return sorted(n for n in os.listdir(d) if n.endswith(".h"))
        host, p = rm
        r = ssh(host, "cd %s && ls -1 *.h 2>/dev/null" % rpath(p))
        return sorted(r.stdout.decode("utf-8", "replace").split())
    except OSError:
        return ["Gauge_Config.h"]


def screen_limits(d):
    """The ranges and limits the built-in views use. Gauge_Config.h wins, then
    the other headers, then the fallbacks in Gauge.cpp. Only the names in
    LIMIT_NAMES (and the #defines they're worked out from) are used - nothing
    else is ever returned."""
    defs = {}
    files = ["Gauge.cpp"] + [n for n in sketch_headers(d) if n != "Gauge_Config.h"] + ["Gauge_Config.h"]
    for name in files:                                 # later file wins
        try:
            t = sketch_read(d, name)
        except OSError:
            continue
        for m in DEFINE.finditer(t.replace("\r\n", "\n")):
            defs[m.group(1)] = m.group(2)
    out = {}
    for k in LIMIT_NAMES:
        if k in defs:
            v = _calc(defs[k], defs)
            if v is not None:
                out[k] = v
    return out


def engine_has(d, word):
    try:
        return word in sketch_read(d, "Gauge_Layout.cpp")
    except OSError:
        return False


def convert_ok(d):
    """The sketch's layout engine has what the converted views need."""
    try:
        t = sketch_read(d, "Gauge_Layout.cpp")
    except OSError:
        return False
    return "peak_label" in t and "by_unit" in t


def sketch_write(d, text):
    """Write Gauge_Layouts.h (CRLF, like the other screen files). The old one is
    kept in BACKUPS on this computer. Returns (where, backup or None)."""
    data = text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")
    ok, msg = sketch_check(d)
    if not ok:
        raise OSError(msg + " - set the screen sketch folder in Settings")
    backup = None
    try:
        old = sketch_read(d)
        os.makedirs(BACKUPS, exist_ok=True)
        backup = os.path.join(BACKUPS, time.strftime("Gauge_Layouts_%Y%m%d_%H%M%S.h"))
        with open(backup, "w", encoding="utf-8", newline="") as f:
            f.write(old)
    except OSError:
        pass                                           # no old file: nothing to keep
    rm = remote_of(d)
    if not rm:
        p = os.path.join(d, "Gauge_Layouts.h")
        with open(p + ".tmp", "wb") as f:
            f.write(data)
        os.replace(p + ".tmp", p)
        return p, backup
    host, p = rm
    f, t = rpath(p, "Gauge_Layouts.h"), rpath(p, "Gauge_Layouts.h.tmp")
    r = ssh(host, "set -e; cat > %s; mv %s %s" % (t, t, f), data=data)
    if r.returncode:
        raise OSError("writing on %s failed: %s" % (host, r.stderr.decode("utf-8", "replace").strip()))
    return "%s:%s/Gauge_Layouts.h" % (host, p.rstrip("/")), backup


class Live:
    """Polls the brain in the background so the page never waits on the car network."""
    def __init__(self):
        self.lock = threading.Lock()
        self.last = None; self.err = "not started"; self.at = 0.0; self.wanted = 0.0
        threading.Thread(target=self.run, daemon=True).start()

    def get(self):
        with self.lock:
            self.wanted = time.time()
            return self.last, self.err, self.at

    def run(self):
        while True:
            with self.lock:
                idle = time.time() - self.wanted > 5
            if idle:                                   # nobody looking: don't poll
                time.sleep(0.25); continue
            url = load_settings()["brain_url"].rstrip("/") + "/"
            try:
                with OPEN(url, timeout=1.5) as r:
                    doc = json.loads(r.read().decode("utf-8", "replace"))
                with self.lock:
                    self.last, self.err, self.at = doc, "", time.time()
            except Exception as e:                     # noqa: BLE001 - report anything to the page
                with self.lock:
                    self.err = "%s: %s" % (url, getattr(e, "reason", e))
                time.sleep(1.0)
            time.sleep(0.1)


LIVE = None


class H(BaseHTTPRequestHandler):
    server_version = "370zLayoutEditor/1"

    def log_message(self, fmt, *a):
        if "--verbose" in sys.argv:
            sys.stderr.write("%s\n" % (fmt % a))

    # -- helpers
    def send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        elif isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def fail(self, msg, code=400):
        self.send(code, {"ok": False, "error": msg})

    def body_json(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n > 4 * 1024 * 1024:
            raise ValueError("too big")
        return json.loads(self.rfile.read(n).decode("utf-8"))

    def local_only(self):
        # Refuse requests from other web pages (the server is localhost-only anyway).
        origin = self.headers.get("Origin")
        host = self.headers.get("Host", "")
        if origin and urlparse(origin).netloc != host:
            self.fail("forbidden", 403); return False
        return True

    # -- routes
    def do_GET(self):
        u = urlparse(self.path); q = parse_qs(u.query)
        if u.path == "/api/settings":
            s = load_settings()
            if (parse_qs(u.query).get("check") or [""])[0]:
                s["sketch_ok"], s["sketch_msg"] = sketch_check(s["sketch_dir"])
            s["sketch_remote"] = remote_of(s["sketch_dir"]) is not None
            s["edition_default"] = DEFAULT_EDITION
            s["editor_version"] = EDITOR_VERSION
            return self.send(200, s)
        if u.path == "/api/files":
            d = load_settings()["layouts_dir"]
            out = []
            if os.path.isdir(d):
                for n in sorted(os.listdir(d)):
                    p = os.path.join(d, n)
                    if n.endswith(".json") and os.path.isfile(p):
                        ed = None                              # which version of the editor made it ("edition" in the file)
                        try:
                            if os.path.getsize(p) < 4000000:
                                with open(p, encoding="utf-8") as f:
                                    e = json.load(f).get("edition")
                                ed = e if e in ("gauge", "can") else None
                        except (OSError, ValueError, AttributeError):
                            pass
                        out.append({"name": n, "mtime": os.path.getmtime(p), "size": os.path.getsize(p), "edition": ed})
            return self.send(200, {"ok": True, "dir": d, "files": out})
        if u.path == "/api/file":
            n = (q.get("name") or [""])[0]
            if not NAME_OK.match(n):
                return self.fail("bad file name")
            p = os.path.join(load_settings()["layouts_dir"], n)
            try:
                with open(p, encoding="utf-8") as f:
                    return self.send(200, f.read(), "application/json")
            except OSError as e:
                return self.fail(str(e), 404)
        if u.path == "/api/export/current":            # what the sketch has now
            try:
                t = sketch_read(load_settings()["sketch_dir"])
                m = re.search(r'R"GLJSON\((.*)\)GLJSON"', t, re.S)
                if not m:
                    return self.fail("Gauge_Layouts.h has no layout in it")
                imgs = images_from_header(t)
                if not imgs:
                    return self.send(200, m.group(1), "application/json")
                try:
                    doc = json.loads(m.group(1))
                except ValueError:
                    return self.send(200, m.group(1), "application/json")
                doc["images"] = imgs
                return self.send(200, doc)
            except OSError as e:
                return self.fail(str(e), 404)
        if u.path == "/api/builtins":
            try:
                return self.send(200, builtin_views(load_settings()["sketch_dir"]))
            except OSError as e:
                return self.send(200, {"ok": False, "views": [], "supported": False, "error": str(e)})
        if u.path == "/api/styles":
            try:
                with open(STYLES, encoding="utf-8") as f:
                    st = json.load(f)
            except (OSError, ValueError):
                st = []
            return self.send(200, {"ok": True, "styles": st if isinstance(st, list) else []})
        if u.path == "/api/live":
            doc, err, at = LIVE.get()
            return self.send(200, {"ok": doc is not None and not err, "error": err,
                                   "age": (time.time() - at) if at else None, "data": doc})
        # static files
        rel = "index.html" if u.path in ("/", "") else u.path.lstrip("/")
        p = os.path.normpath(os.path.join(WEB, rel))
        if not p.startswith(WEB + os.sep) or not os.path.isfile(p):
            return self.send(404, "not found", "text/plain")
        with open(p, "rb") as f:
            self.send(200, f.read(), TYPES.get(os.path.splitext(p)[1], "application/octet-stream"))

    def do_POST(self):
        if not self.local_only():
            return
        u = urlparse(self.path); q = parse_qs(u.query)
        try:
            body = self.body_json()
        except ValueError as e:
            return self.fail("bad request: %s" % e)
        if u.path == "/api/settings":
            s = load_settings()
            for k in DEFAULTS:
                if isinstance(body.get(k), str) and body[k].strip():
                    v = body[k].strip()
                    s[k] = os.path.expanduser(v) if k.endswith("_dir") and not remote_of(v) else v
            save_settings(s)
            return self.send(200, {"ok": True})
        if u.path == "/api/file":
            n = (q.get("name") or [""])[0]
            if not NAME_OK.match(n):
                return self.fail("file name: letters, numbers, spaces, - _ . ( ) and ending .json")
            try:
                check_layout(body)
            except ValueError as e:
                return self.fail(str(e))
            d = load_settings()["layouts_dir"]
            os.makedirs(d, exist_ok=True)
            p = os.path.join(d, n)
            tmp = p + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(body, f, indent=1, ensure_ascii=False); f.write("\n")
            os.replace(tmp, p)
            return self.send(200, {"ok": True, "path": p})
        if u.path == "/api/export":
            try:
                check_layout(body)
                text = header_text(body)
            except ValueError as e:
                return self.fail(str(e))
            try:
                p, bak = sketch_write(load_settings()["sketch_dir"], text)
            except OSError as e:
                return self.fail(str(e))
            return self.send(200, {"ok": True, "path": p, "backup": bak})
        if u.path == "/api/styles":                    # the whole favourites list
            st = body.get("styles") if isinstance(body, dict) else None
            if not isinstance(st, list) or len(st) > 200 or not all(isinstance(x, dict) and isinstance(x.get("name"), str) for x in st):
                return self.fail("bad styles list")
            os.makedirs(CFG_DIR, exist_ok=True)
            tmp = STYLES + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(st, f, indent=1, ensure_ascii=False); f.write("\n")
            os.replace(tmp, STYLES)
            return self.send(200, {"ok": True})
        if u.path == "/api/header":                    # the header text, for downloading
            try:
                check_layout(body)
                return self.send(200, {"ok": True, "text": header_text(body).replace("\n", "\r\n")})
            except ValueError as e:
                return self.fail(str(e))
        self.fail("not found", 404)


def main():
    global LIVE, DEFAULT_EDITION
    ap = argparse.ArgumentParser(description="370Z gauge layout editor")
    ap.add_argument("--port", type=int, default=8370)
    ap.add_argument("--no-browser", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--edition", choices=("gauge", "can"), default="gauge",
                    help="which version the page opens on: the standalone oil gauge (default) or the full CAN-bus screens")
    ap.add_argument("--lan", action="store_true",
                    help="listen on the network too (no password: only on your own network)")
    a = ap.parse_args()
    DEFAULT_EDITION = a.edition
    for w in ("preview.wasm", "preview_can.wasm"):
        if not os.path.isfile(os.path.join(WEB, w)):
            sys.exit("web/%s is missing - unpack the whole editor folder" % w)
    url = "http://127.0.0.1:%d/" % a.port
    try:
        srv = ThreadingHTTPServer(("0.0.0.0" if a.lan else "127.0.0.1", a.port), H)
    except OSError:
        # Already running (e.g. started from the app menu earlier): just show it.
        try:
            with OPEN(url + "api/settings", timeout=2) as r:
                other = json.loads(r.read())
            if other.get("editor_version") != EDITOR_VERSION:
                sys.exit("An editor is already running on port %d, and it is %s - not this one (%s).\n"
                         "Stop it first (if it is a service: sudo systemctl stop 370z-layout-editor), or start this one with --port 8371."
                         % (a.port, ("version " + other["editor_version"]) if other.get("editor_version") else "an older version", EDITOR_VERSION))
            print("The editor is already running: %s" % url)
            if not a.no_browser:
                webbrowser.open(url)
            return
        except Exception:                              # noqa: BLE001
            sys.exit("port %d is used by something else - try --port 8371" % a.port)
    LIVE = Live()
    print("370Z layout editor %s: %s  (Ctrl+C to stop)" % (EDITOR_VERSION, url))
    if a.lan:
        import socket
        try:
            k = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); k.connect(("10.255.255.255", 1)); ip = k.getsockname()[0]; k.close()
        except OSError:
            ip = socket.gethostname()
        print("from other computers on your network: http://%s:%d/" % (ip, a.port))
    if not a.no_browser and not a.lan:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
