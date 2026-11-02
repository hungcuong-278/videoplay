# 10 — Công cụ đo lường

Thư mục `vmp_tools/` chứa các công cụ dùng để **đo** hiệu năng và **kiểm chứng** các mục đã làm.

Đây **không phải** sản phẩm. Không nạp vào `videoMetadataPlayer.pro`.

---

## 1. Danh sách

| Thư mục | Công cụ | Dùng để |
|---|---|---|
| `keyframe_test/` | `keyframeIndexTool` | in mục lục keyframe, so với ffprobe |
| `keyframe_test/` | `countIndexed` | đo số buffer vào decoder mỗi lần seek |
| `decode_count/` | `count` | đo số buffer/seek, có và không có gate |
| `seek_lat/` | `lat` | đo độ trễ seek + ID mã vạch của hình hạ cánh |
| `seek_test/` | `step` | đo tua từng frame (← / →) |
| `drag_test/` | `drag` | mô phỏng kéo thanh trượt |
| `seek_tsan/` | `chaos` | gây race để ThreadSanitizer bắt |
| `cpu_test/` | `cpu` | đo CPU khi phát |
| `bc_test/` | `bcTest` | kiểm tra bộ đọc mã vạch |
| `tools/` | `c128_all.py` | sinh ảnh mẫu cho bộ đọc mã vạch |

---

## 2. Cách build chung

Mỗi tool có `.pro` riêng, trừ nhóm dùng chung `tool/tool.pro`.

### 2.1 Nhóm có `.pro` riêng

```bash
cd vmp_tools/keyframe_test
qmake keyframe_test.pro && make -j"$(nproc)"
```

| File `.pro` | Target |
|---|---|
| `keyframe_test/keyframe_test.pro` | `keyframeIndexTool` |
| `keyframe_test/count_indexed.pro` | `countIndexed` |

### 2.2 Nhóm dùng chung `tool/tool.pro`

Các tool `lat.cpp`, `step.cpp`, `drag.cpp`, `chaos.cpp`, `cpu.cpp` đều dùng `VideoPlayer`.

```bash
cd vmp_tools/seek_lat
cp ../tool/tool.pro .
qmake tool.pro && make -j"$(nproc)"
```

**Vì sao dùng chung:** cả 5 tool đều cần `VideoPlayer` + `BarcodeReader`, khác nhau ở `SOURCES` (tên file `.cpp` của tool) và `TARGET`.

**Lưu ý:** `tool/tool.pro` đang trỏ tới

```
VMP = $$(HOME)/cuong_workspace/video_metadata_player
```

Nếu đường dẫn máy anh khác, sửa `VMP` cho đúng.

### 2.3 `count` — build bằng g++ trực tiếp

```bash
cd vmp_tools/decode_count
g++ -O2 -std=c++11 count.cpp -o count \
    $(pkg-config --cflags --libs gstreamer-1.0)
```

Build khoảng 5 giây, mỗi lần chạy 10–30 giây.

### 2.4 `bcTest` — chỉ cần `BarcodeReader`

```bash
cd vmp_tools/bc_test
cat > tool.pro <<'PRO'
QT += gui
CONFIG += c++11 link_pkgconfig console
PKGCONFIG += gstreamer-1.0
VMP = $$(HOME)/cuong_workspace/video_metadata_player
INCLUDEPATH += $$VMP
HEADERS += $$VMP/barcodeReader.hpp
SOURCES += bcTest.cpp $$VMP/barcodeReader.cpp
PRO
qmake tool.pro && make -j"$(nproc)"
```

---

## 3. Chi tiết từng tool

### 3.1 `keyframeIndexTool` — in mục lục

```bash
./vmp_tools/keyframe_test/keyframeIndexTool video.mp4
```

In ra:

- Số keyframe tìm được
- Thời gian quét
- Thời lượng chính xác (nếu có)
- Khoảng cách lớn nhất giữa hai keyframe
- Danh sách từng keyframe (thời gian)

**Dùng để:** đối chiếu với `ffprobe`, và phát hiện mục lục sai.

### 3.2 `countIndexed` — đo hiệu quả của mục lục

```bash
./vmp_tools/keyframe_test/countIndexed long60.ts
```

Chạy 8 seek chính xác, **hai lần**: tắt index rồi bật index. In số buffer đi vào decoder mỗi lần.

**Đây là công cụ đo quan trọng nhất của mục 10.1.**

| | Kỳ vọng |
|---|---|
| Tắt index | ~642 buffer/seek |
| Bật index | ~117 buffer/seek |

### 3.3 `count` — đo số buffer/seek (không có gate)

```bash
./count long60.ts
./count long60.mp4
```

Dùng `playbin` thuần, **không** có gate của project.

**Vì sao vẫn cần:** đây là **đường cơ sở** để so sánh. Nếu `countIndexed` ra 117 mà `count` ra 117, thì gate đang hoạt động đúng.

### 3.4 `lat` — đo độ trễ seek + đúng ID

```bash
./vmp_tools/seek_lat/lat video.mp4
```

Chạy 8 seek chính xác, in độ trễ và **ID mã vạch** của hình hạ cánh.

**Cách kiểm chứng mục 10.1:** ID phải đúng **8/8**.

**Nếu ID sai** → lỗi A3 trong `08_loi_tien_mung.md` (dung sai 2 ms).

### 3.5 `step` — tua từng frame

```bash
./vmp_tools/seek_test/step video.mp4
```

Kiểm tra lỗi 8.6 của hồ sơ cũ (lùi 1 frame bị kẹt hoặc nhảy cóc).

### 3.6 `drag` — mô phỏng kéo thanh trượt

```bash
./vmp_tools/drag_test/drag video.mp4
```

Mô phỏng kéo nhanh để kiểm tra cơ chế coalescing seek (`m_seekInFlight` + `m_pendingSeekMs`).

### 3.7 `chaos` — gây race

```bash
./vmp_tools/seek_tsan/chaos video.mp4
```

Tua liên tục, đổi tốc độ, mở/đóng file. Chạy dưới TSan.

### 3.8 `cpu` — đo CPU khi phát

```bash
./vmp_tools/cpu_test/cpu video.mp4
```

Đo CPU trong lúc phát. Dùng để kiểm chứng mục 10.2 (vẽ bằng texture).

### 3.9 `bcTest` — kiểm tra bộ đọc mã vạch

```bash
./vmp_tools/bc_test/bcTest video.mp4
```

In số frame đọc được / tổng số frame, và danh sách ID.

**Dùng để:** kiểm chứng "khớp 100%" sau khi cắt (mục 7.4 trong `09_keichung_chi_tiet.md`).

### 3.10 `c128_all.py` — sinh ảnh mẫu

```bash
python3 vmp_tools/tools/c128_all.py
```

Sinh ảnh mã vạch Code 128 ở nhiều độ phân giải để kiểm tra bộ đọc.

---

## 4. Công cụ dùng nhiều nhất

| Tình huống | Lệnh |
|---|---|
| Kiểm tra mục lục đúng chưa | `keyframeIndexTool video.mp4` |
| Đo hiệu quả của mục lục | `countIndexed long60.ts` |
| Seek chính xác có đúng hình không | `lat video.mp4` |
| CSV cắt có khớp video không | `bcTest cut.ts` |
| Có race không | `chaos video.mp4` dưới TSan |

---

## 5. Quy ước khi thêm tool mới

1. Đặt trong thư mục con riêng, tên thư mục = tên nhóm.
2. Có `.pro` riêng, hoặc dùng chung `tool/tool.pro`.
3. `VMP` trỏ về thư mục gốc project.
4. **Không** thêm vào `videoMetadataPlayer.pro`.
5. **Không** viết comment tiếng Việt trong code (chuẩn team).
6. Ghi kết quả đo vào `09_keichung_chi_tiet.md`.

---

## 6. Về thư mục `vmp_tools` trong git

Thư mục này **có** trong git, vì:

1. Các kết quả đo của máy công ty cần tái lập được ở máy khác.
2. Anh Trí có thể tự chạy lại để kiểm chứng.

Nhưng nó **không** thuộc sản phẩm:

- Không được link vào `videoMetadataPlayer.pro`.
- Không nên sửa khi sửa sản phẩm (trừ khi API đổi).
- `.gitignore` không ignore thư mục này.
