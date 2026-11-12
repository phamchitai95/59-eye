"""HTTP API Dahua gia lap (khong xac thuc) cho LitePSS: mediaFileFind.cgi + ChannelTitle.
Chay: python fake_nvr_http.py  (cong 8080)"""
import datetime as dt
import http.server
import urllib.parse

objects = {}


def records(day: dt.date):
    """Moi gio co 1 doan ghi 0-50 phut, gio chan co them doan su kien."""
    out = []
    for h in range(24):
        s = dt.datetime.combine(day, dt.time(h, 0))
        out.append((s, s + dt.timedelta(minutes=50), "Timing"))
        if h % 2 == 0:
            out.append((s + dt.timedelta(minutes=52), s + dt.timedelta(minutes=58), "Event"))
    return out


class H(http.server.BaseHTTPRequestHandler):
    def reply(self, body):
        data = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = dict(urllib.parse.parse_qsl(u.query))
        a = q.get("action")
        if u.path.endswith("configManager.cgi"):
            return self.reply("".join(f"table.ChannelTitle[{i}].Name=Cam gia lap {i+1}\r\n" for i in range(16)))
        if a == "factory.create":
            oid = str(1000 + len(objects))
            objects[oid] = []
            return self.reply(f"result={oid}\r\n")
        if a == "findFile":
            start = dt.datetime.strptime(q["condition.StartTime"], "%Y-%m-%d %H:%M:%S")
            objects[q["object"]] = records(start.date())
            return self.reply("OK\r\n")
        if a == "findNextFile":
            lst = objects.get(q["object"], [])
            n = int(q.get("count", 100))
            chunk, objects[q["object"]] = lst[:n], lst[n:]
            lines = [f"found={len(chunk)}"]
            for i, (s, e, f) in enumerate(chunk):
                lines += [f"items[{i}].StartTime={s:%Y-%m-%d %H:%M:%S}", f"items[{i}].EndTime={e:%Y-%m-%d %H:%M:%S}",
                          f"items[{i}].Type=dav", f"items[{i}].Flags[0]={f}"]
            return self.reply("\r\n".join(lines) + "\r\n")
        if a in ("close", "destroy"):
            return self.reply("OK\r\n")
        self.send_error(404)

    def log_message(self, *args):
        pass


print("Fake NVR HTTP: http://127.0.0.1:8080")
http.server.ThreadingHTTPServer(("127.0.0.1", 8080), H).serve_forever()
