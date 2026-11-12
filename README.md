# 59-eye (C++ / Qt 6 + GStreamer)

Phần mềm xem camera của Trung tâm 59 cho các đầu ghi Dahua trong LAN. **Không dùng NetSDK**:
- Xem trực tiếp: RTSP `cam/realmonitor` (luồng phụ cho lưới, luồng chính khi phóng to / 1x1).
- Xem lại: tìm đoạn ghi bằng HTTP API `mediaFileFind.cgi` (Digest), phát bằng RTSP `cam/playback`.
- Giải mã: GStreamer `decodebin` tự chọn bộ giải mã GPU (Windows: `d3d11h265dec`/`d3d12h265dec`,
  Ubuntu: `vah265dec`/`nvh265dec`), vẽ thẳng lên cửa sổ native (`d3d11videosink` / `glimagesink`).

## Cấu hình
Chép `config.example.json` thành `config.json` (cạnh `litepss.exe`, tức trong `build\`) rồi điền:
`host` = IP Tailscale của đầu ghi, `user`/`password`, `channels` = số kênh.
> Mật khẩu đang lưu dạng chữ thường trong file — chấp nhận cho PoC, bản thật cần kho bảo mật của hệ điều hành.

## Build & chạy trên Windows
Công cụ đã cài sẵn trong `..\_deps` (Qt 6.8.3 MinGW, MinGW 13.1, GStreamer 1.26.11 MinGW, CMake, Ninja).
```
build.bat
run.bat
```

## Build trên Ubuntu 24.04
```
sudo apt install build-essential cmake ninja-build qt6-base-dev libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
  gstreamer1.0-libav gstreamer1.0-gl
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
cp config.json build/ && ./build/litepss
```
Ứng dụng tự chạy qua XWayland (`QT_QPA_PLATFORM=xcb`) vì video nhúng cần cửa sổ X11.

## Cách dùng
- **Xem trực tiếp**: chọn 1x1/2x2/3x3/4x4, nhấp đúp kênh bên trái để đưa vào ô đang chọn (viền xanh),
  hoặc "Tự điền kênh". Nhấp đúp một ô để phóng to (chuyển luồng chính), nhấp đúp lần nữa để quay lại.
  Mỗi ô hiện: tên kênh · bộ giải mã đang dùng · độ phân giải · kbps. Mất kết nối tự thử lại sau 3 giây.
- **Xem lại**: chọn kênh + ngày → thanh thời gian (xanh = ghi thường, cam = sự kiện).
  Nhấp vào thanh để phát từ thời điểm đó, lăn chuột để phóng to, kéo để di chuyển.
  Tốc độ x2/x4/x8 gửi lệnh RTSP `Scale`; nếu hình chạy sai tốc độ hãy thử ô "Tắt đồng bộ khi tua nhanh".
- **Âm thanh**: nút loa trên thanh tiêu đề mỗi ô, mỗi lúc chỉ 1 ô phát tiếng (bật ô này thì ô khác tắt).
  Bật/tắt sẽ kết nối lại ô đó (~1 giây). Hỗ trợ G.711A/U (PCMA/PCMU), AAC, G.726. Camera phải bật Audio
  trong cấu hình mã hóa của đầu ghi. Tab Xem lại cũng có nút loa trên ô video.
- **Âm lượng**: thanh trượt ở góc phải thanh công cụ (mặc định 60%, lưu trong `build\ui.ini`). Bị hú là do
  mic camera thu lại tiếng loa (phản hồi âm thanh) → giảm âm lượng / dùng tai nghe / để loa xa camera.
- **Đèn camera**: nút bóng đèn trên mỗi ô xem trực tiếp → Bật / Tắt / Tự động. App tự lấy IP + tài khoản camera
  từ cấu hình RemoteDevice của đầu ghi (chỉ giữ trong bộ nhớ). Cách điều khiển theo loại camera:
  Dahua → `Lighting_V2` (đèn trắng, cả 3 profile); ONVIF có lệnh PTZ `tt:Lamp|On/Off` (TR38Y51, K7T1S4);
  ONVIF chỉ có IrCutFilter → ép chế độ đêm/ngày. Kiểm tra chỉ đọc: `litepss.exe --light-detect 0 out.txt 1,2,3`.
- **Chỉnh hình**: nút thanh trượt trên mỗi ô → độ sáng, tương phản, độ đậm màu, sắc độ (chạy trên GPU qua
  `d3d12videosink`). Mẫu: Gốc / Nịnh mắt (mặc định) / Ban đêm. "Lưu cho kênh này" hoặc "Áp dụng cho mọi kênh";
  lưu trong `build\image.ini`. Không có làm nét vì GStreamer không có bộ làm nét chạy GPU; thay bằng thu phóng
  anisotropic. Trên Ubuntu (`glimagesink`) chưa chỉnh màu được.
- **Chẩn đoán HTTP**: đặt biến môi trường `LITEPSS_HTTP_LOG=1` → ghi `build\http.log` (không ghi mật khẩu).
- **Thanh đo phía dưới**: CPU, RAM, GPU giải mã, GPU 3D, RAM GPU, băng thông mạng tổng.
  Gõ `SmartPSS.exe` vào ô "So sánh với tiến trình" để đo song song; "Ghi CSV" lưu số liệu mỗi giây.

## Kết quả đo trên máy dev (i5-7200U, Intel HD 620, 8 GB) với đầu ghi giả lập
16 luồng phụ H.265 Main 704x576, bộ giải mã tự chọn `d3d12h265dec` (GPU):

| Sink | CPU app | RAM riêng | GPU giải mã | GPU 3D | RAM GPU |
|---|---|---|---|---|---|
| `d3d12videosink` (mặc định) | ~10% | **~337 MB** | ~6–8% | ~3% | ~300 MB |
| `d3d11videosink` | ~9% | ~687 MB | ~11% | ~11% | ~423 MB |
| App mở, chưa có luồng | 0.1% | 22 MB | – | – | – |

Xem lại 1 luồng: CPU 0.4%, RAM 185 MB. Tìm bản ghi (HTTP giả lập): ~180 ms.

### Đo trên NVR 1 thật (luồng phụ, 20–25 fps gốc)
| Cấu hình | 4 luồng | 16 luồng |
|---|---|---|
| Cũ: queue 3 khung + leaky, đệm 200 ms | 2 luồng chỉ còn **3–5 fps** (giật) | – |
| Mới: queue không vứt khung, đệm 200 ms | cả 4 luồng ~20 fps | 15/16 luồng 18.5–24 fps, rớt 0–6% |
| Mới, đệm 500 ms | ~20 fps | tương tự 200 ms → giữ 200 ms |

`tools\realtest.ps1 -Latency 200 -Layout 16 -Tag x` chạy đo trên đầu ghi thật (đọc `build\config.json`).

## Công cụ thử không cần đầu ghi thật (`tools\`)
- `fake_nvr_rtsp.exe` — RTSP H.265 tại `rtsp://127.0.0.1:8554/cam/realmonitor` và `/cam/playback`.
- `python fake_nvr_http.py` — HTTP API giả (`mediaFileFind.cgi`, `ChannelTitle`) cổng 8080.
- `bench.ps1 -Sink d3d12videosink -Layout 16 -Seconds 35` — chạy đo tự động, in trung bình 10 giây cuối.
- Tham số dòng lệnh của app: `--layout 16 --autofill --playback --record --compare SmartPSS.exe --screenshot a.png --quit-after 40`.

## Những điều PoC cần kiểm chứng
1. Bộ giải mã hiện trên ô có phải loại GPU (`d3d11h265dec`, `d3d12h265dec`, `vah265dec`…) không, hay rơi về `avdec_h265` (CPU).
2. Lưới 16 luồng phụ H.265: CPU/GPU/RAM so với SmartPSS.
3. Đầu ghi có nhận `Scale` không, băng thông khi x8 qua Tailscale.
4. Thời gian tìm bản ghi và độ trễ khi tua (mở lại RTSP).

## Tài khoản và phân quyền
- Lần đầu mở app: tạo tài khoản quản trị. Tài khoản lưu trong `users.json` (mật khẩu băm PBKDF2-SHA256).
- Quản trị: toàn quyền, có tab **Cài đặt**. Nhân viên: chỉ các quyền được chọn (xem trực tiếp, xem lại,
  trích MP4, âm thanh, đèn, chỉnh màu) và các kênh được chọn.
- Đây là phân quyền trong app: ai có quyền sửa file trên máy vẫn vượt qua được.

## Tab Cài đặt (chỉ quản trị)
- Thêm/sửa/xoá đầu ghi Dahua (nút "Kiểm tra kết nối" tự đếm số kênh) và camera RTSP lẻ (chỉ xem trực tiếp).
- Bật/tắt từng kênh: kênh tắt bị ẩn với mọi tài khoản. Chỉ lưu trong `config.json`, không đổi cấu hình đầu ghi.

## Xem lại
- Ô ngày mở lịch: ngày tô màu là có bản ghi, chấm cam là có sự kiện (1 lần tìm cho cả tháng).
- **Trích MP4**: chọn từ/đến (tối đa 2 giờ), luồng chính/phụ, kèm tiếng (G.711 đổi sang AAC).
  Không nén lại hình nên giữ nguyên chất lượng; chạy theo tốc độ thực (1 phút ≈ 1 phút).
  Mặc định lưu vào `Videos\59-eye`. Luồng phụ chỉ trích được nếu đầu ghi có ghi luồng phụ.

## Thư mục dữ liệu
- Bản cài đặt: `C:\ProgramData\59-eye` (config.json, users.json, image.ini, ui.ini, log), dùng chung mọi tài khoản Windows.
- Bản dev: nếu cạnh exe có `config.json` hoặc `portable.txt` thì dùng thư mục exe (`build\`).

## Đóng gói và cập nhật OTA
```
powershell -ExecutionPolicy Bypass -File tools\package.ps1 -Notes "Nội dung bản mới"
```
→ `dist\59-eye-setup-<version>.exe` (cài vào `C:\Program Files\59-eye`) và `dist\latest.json`.
Phiên bản lấy từ `project(EYE59 VERSION x.y.z)` trong CMakeLists.txt — tăng trước mỗi lần phát hành.

OTA: app đọc `latest.json` từ `updateUrl` (trong config.json) hoặc URL nhúng lúc build, so phiên bản,
tải bộ cài, kiểm tra SHA-256, cài im lặng rồi tự mở lại. Phát hành lên GitHub Releases (repo public, cần `gh auth login`):
```
powershell -ExecutionPolicy Bypass -File tools\package.ps1 -Notes "..." -Repo <chu>/<repo>
```
URL OTA: `https://github.com/<chu>/<repo>/releases/latest/download/latest.json`.
Kiểm tra tay: menu tài khoản (góc phải) → "Kiểm tra cập nhật".
