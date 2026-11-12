"""Gom 59-eye.exe + Qt + GStreamer (chi plugin can dung) vao dist\\59-eye de dong goi.

Chay: python tools\\deploy.py   (sau build.bat)
Thu tu uu tien DLL giong run.bat: ban trong GStreamer\\bin thang ban cua Qt/MinGW khi trung ten.
"""
import os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEPS = os.path.join(os.path.dirname(ROOT), '_deps')
QT = os.path.join(DEPS, 'Qt', '6.8.3', 'mingw_64')
MINGW = os.path.join(DEPS, 'Qt', 'Tools', 'mingw1310_64')
GST = os.path.join(DEPS, 'gst', 'PFiles64', 'gstreamer', '1.0', 'mingw_x86_64')
DIST = os.path.join(ROOT, 'dist', '59-eye')
EXE = os.path.join(ROOT, 'build', '59-eye.exe')

PLUGINS = ['coreelements', 'rtsp', 'rtp', 'rtpmanager', 'udp', 'tcp', 'playback', 'typefindfunctions',
           'videoparsersbad', 'd3d12', 'd3d11', 'opengl', 'libav', 'isomp4', 'voaacenc', 'alaw', 'mulaw',
           'audioconvert', 'audioresample', 'volume', 'autodetect', 'wasapi', 'videoconvertscale',
           'audioparsers', 'app', 'directsound']
ORT_DLL = os.path.join(DEPS, 'onnxruntime-win-x64-1.31.1', 'lib', 'onnxruntime.dll')

OBJDUMP = os.path.join(MINGW, 'bin', 'objdump.exe')
SEARCH = [os.path.join(GST, 'bin'), os.path.join(QT, 'bin'), os.path.join(MINGW, 'bin')]


def deps_of(path):
    out = subprocess.run([OBJDUMP, '-p', path], capture_output=True, text=True, errors='ignore').stdout
    return re.findall(r'DLL Name:\s*(\S+)', out)


def find_dll(name):
    for d in SEARCH:
        p = os.path.join(d, name)
        if os.path.isfile(p):
            return p
    return None  # DLL he thong (kernel32, d3d12...)


def main():
    if not os.path.isfile(EXE):
        sys.exit('Chua build: ' + EXE)
    if os.path.isdir(DIST):
        shutil.rmtree(DIST)
    os.makedirs(DIST)
    shutil.copy2(EXE, DIST)
    shutil.copy2(ORT_DLL, DIST)  # dem nguoi (mo hinh tai ve lan dau dung)

    env = dict(os.environ, PATH=os.path.join(QT, 'bin') + os.pathsep + os.path.join(MINGW, 'bin') + os.pathsep + os.environ['PATH'])
    subprocess.run([os.path.join(QT, 'bin', 'windeployqt.exe'), '--release', '--no-translations',
                    '--no-system-d3d-compiler', '--no-opengl-sw', '--no-quick-import',
                    os.path.join(DIST, '59-eye.exe')], check=True, env=env, stdout=subprocess.DEVNULL)

    plug_dst = os.path.join(DIST, 'lib', 'gstreamer-1.0')
    os.makedirs(plug_dst)
    roots = [os.path.join(DIST, '59-eye.exe')]
    for p in PLUGINS:
        src = os.path.join(GST, 'lib', 'gstreamer-1.0', f'libgst{p}.dll')
        if not os.path.isfile(src):
            print('  (khong co plugin)', p)
            continue
        shutil.copy2(src, plug_dst)
        roots.append(src)
    scan_dst = os.path.join(DIST, 'libexec', 'gstreamer-1.0')
    os.makedirs(scan_dst)
    scanner = os.path.join(GST, 'libexec', 'gstreamer-1.0', 'gst-plugin-scanner.exe')
    shutil.copy2(scanner, scan_dst)
    roots.append(scanner)

    # Bao dong phu thuoc DLL (de quy)
    seen, queue, copied = set(), list(roots), 0
    while queue:
        cur = queue.pop()
        for name in deps_of(cur):
            key = name.lower()
            if key in seen:
                continue
            seen.add(key)
            src = find_dll(name)
            if not src:
                continue
            dst = os.path.join(DIST, name)
            # ban GStreamer thang khi trung ten (giong thu tu PATH cua run.bat)
            if not os.path.isfile(dst) or os.path.dirname(src) == os.path.join(GST, 'bin'):
                shutil.copy2(src, dst)
                copied += 1
            queue.append(src)
    # Thu muc portable khong duoc co config.json -> du lieu nam o %ProgramData%\59-eye
    size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(DIST) for f in fs)
    print(f'dist: {DIST}  ({size / 1048576:.0f} MB, {copied} DLL phu thuoc)')


if __name__ == '__main__':
    main()
