# 00 — Đọc nhanh: đang ở đâu, việc tiếp theo là gì

## 1. Bối cảnh

Hồ sơ bàn giao đầu tiên (`video_metadata_player_handoff.md`, 4 222 dòng) mô tả trạng thái project ở commit 5 của máy công ty, và nêu ba vấn đề đang dở:

| Mục | Vấn đề | Đo được |
|---|---|---|
| 10.1 | CPU 400% mỗi lần tua chính xác trên file TS | 642 frame giải mã / tua, so 117 frame nếu đóng gói MP4 |
| 10.2 | Hình khựng khi phát | 42–114% một lõi chỉ để vẽ, `QQuickPaintedItem` |
| 10.3 | Thời lượng file TS nhảy liên tục | 117:08 → 116:03, thật là 120:00.898 |

và ba việc chưa làm (mục 11): overlay thông số, cắt video không encode + CSV khớp 100%, đồ thị pan/tilt.

**Hồ sơ này** mô tả code đã viết cho cả sáu việc đó.

## 2. Trạng thái thật

> **Code đã viết xong. Code chưa từng chạy một lần.**

Lý do: máy dùng để viết là Windows 10, không cài Qt 5.12, không cài GStreamer, không có `qmake`. Mọi kiểm tra đã làm là **kiểm tra tĩnh**:

- Cân bằng ngoặc `{}` và `()` trong tất cả file.
- Quy tắc include: mỗi `.cpp` chỉ include header của chính nó.
- Độ dài dòng ≤ 80 ký tự.
- Đối chiếu khai báo trong `.hpp` với định nghĩa trong `.cpp` (không có hàm nào khai báo mà không định nghĩa).
- Guard `#ifndef` / `#endif` đúng tên.

Chưa làm được: build, chạy, đo, xem hình.

**Hệ quả:** mọi con số trong tài liệu này là **kỳ vọng**, không phải **đo được**. Chỗ nào ghi "kỳ vọng" thì đừng trích như kết quả.

## 3. Bảng công việc

| # | Việc | File | Trạng thái | Ghi chú |
|---|---|---|---|---|
| 1 | Mục lục keyframe (quét nền) | `src/index/keyframeIndex.hpp/.cpp` | ✅ xong | Cache `.vmpidx` |
| 2 | Bộ đọc NAL dùng chung | `src/index/nalParser.hpp/.cpp` | ✅ xong | H.264 + H.265 |
| 3 | Nối mục lục vào gate | `src/player/videoPlayer.hpp/.cpp` | ✅ xong | `WAIT_UNTIL_ENTRY` |
| 4 | Thời lượng chính xác | `src/index/keyframeIndex.cpp`, `src/player/videoPlayer.cpp` | ✅ xong | Sửa luôn 10.3 |
| 5 | Tiến độ quét trên QML | `qml/main.qml` | ✅ xong | "Indexing 37%" |
| 6 | Vẽ bằng texture | `src/player/frameView.hpp/.cpp` | ✅ xong | Thay `QQuickPaintedItem` |
| 7 | Overlay thông số | `qml/main.qml`, `src/player/videoPlayer.cpp` | ✅ xong | Phím `O` |
| 8 | Cắt video | `src/cutter/videoCutter.hpp/.cpp` | ⚠️ xong, **chỉ TS** | Xem `06_cat_video.md` |
| 9 | Điều phối cắt | `src/cutter/cutController.hpp/.cpp` | ✅ xong | Mark In / Mark Out |
| 10 | Lọc CSV theo ID thật | `src/metadata/metadataStore.cpp` | ✅ xong | `writeRows()` |
| 11 | Đồ thị pan/tilt | `src/views/panTiltGraph.hpp/.cpp` | ✅ xong | Chưa bấm để tua |
| 12 | Dọn dẹp cấu trúc repo | toàn repo | ✅ xong | 5 module trong `src/` |

## 3.1 Kiểm tra tĩnh đã làm

Trên 20 file sản phẩm (`src/**/*.cpp`, `src/**/*.hpp`):

| Kiểm tra | Kết quả |
|---|---|
| Cân bằng `{}` `()` `[]` (bỏ qua comment và chuỗi) | ✅ 0 lỗi |
| Độ dài dòng ≤ 80 ký tự | ✅ 0 lỗi |
| Mỗi `.cpp` chỉ include header của chính nó | ✅ 0 lỗi |
| Guard `#ifndef` / `#define` / `#endif` đúng tên | ✅ 0 lỗi |
| Không có tab, không có trailing whitespace | ✅ 0 lỗi |
| File kết thúc bằng newline | ✅ 0 lỗi |

Chưa làm được: build, chạy, đo, xem hình.

## 4. Việc cần làm tiếp, theo thứ tự nên làm

### Bước 1 — Build (30–60 giây lần đầu)

```bash
git clone https://github.com/hungcuong-278/videoplay.git
cd videoplay
mkdir -p build && cd build
qmake ../videoMetadataPlayer.pro && make -j4
```

Nếu lỗi: đọc `08_loi_tien_mung.md` mục A8. Lỗi nhiều khả năng là thiếu include.

### Bước 2 — Kiểm chứng mục lục trước khi tin vào nó

Mục lục là nền của cả mục 10.1. Nếu mục lục sai thì mọi thứ sau cũng sai.

```bash
# Số keyframe ffprobe thấy (file mẫu: 28)
ffprobe -v error -select_streams v:0 \
  -show_entries frame=key_frame -of csv=p=0 video.mp4 \
  | grep -c '^1$'

# Mục lục của em thấy
cd vmp_tools/keyframe_test
qmake keyframe_test.pro && make
./keyframeIndexTool /path/to/video.mp4 28
```

Phải khớp. Nếu lệch, đọc `02_muc_luc_keyframe.md` mục "Khi số keyframe lệch".

### Bước 3 — Đo hiệu quả của mục lục

```bash
qmake count_indexed.pro && make
./countIndexed /path/to/long60.ts
```

Kết quả mong đợi: số buffer vào decoder mỗi seek chính xác **giảm mạnh** khi bật "Use index".

### Bước 4 — Test từng mục

Theo thứ tự trong `09_keichung_chi_tiet.md`.

### Bước 5 — Việc còn dở nếu còn thời gian

| Việc | Vì sao chưa làm |
|---|---|
| MP4 trong `copyStream` | Cần nhánh `qtdemux ! mp4mux`; hồ sơ cũ nói máy công ty có `mp4mux` |
| Bấm đồ thị để tua | Cần tra ngược ID → PTS, tài liệu 10.1 gợi ý dùng mục lục |
| Đo lại hiệu năng | Chưa có máy để đo |
| ThreadSanitizer trên code mới | Cần `libtsan`, máy công ty thiếu |

## 5. Bốn chỗ dễ hỏng nhất

Em đã nghĩ trước và ghi đầy đủ trong `08_loi_tien_mung.md`. Tóm tắt:

### 5.1 Mốc thời gian của TS lệch nhau giữa index và decoder

File TS bắt đầu ở PTS 3600 s nhưng hiển thị từ 125 ms. `tsdemux` giữ nguyên PTS gốc và chỉ dịch mốc bằng segment.

→ `KeyframeIndex` lưu **cả hai**: stream time (ms) để tra mục lục, và **PTS thô (ns)** để so sánh trong gate. Gate so sánh bằng PTS thô.

**Nếu ai đó đổi gate sang so sánh stream time, seek sẽ không bao giờ hạ cánh.**

### 5.2 Dung sai 2 ms có thể quá nhỏ

`m_entryToleranceNs = 2 * GST_MSECOND`. Nếu PTS của hình vào decoder lệch vài trăm µs so với PTS của bộ phân tích, hình đích có thể bị bỏ qua.

→ Nếu hình đích không hiện, tăng lên 20 ms. Đo lại bằng `countIndexed` để chắc không bỏ quá nhiều.

### 5.3 `query_position` không đáng tin với TS

`copyStream` dùng `gst_element_query_position` để biết đã tới cuối đoạn cắt chưa. Với `tsdemux`, vị trí có thể lệch.

→ Nếu file cắt bị cắt sớm/muộn, phải đổi sang đếm hình qua nhánh `appsink`.

### 5.4 Overlay không có dữ liệu lúc đầu

`updateVideoInfo()` được gọi ở `ASYNC_DONE` đầu tiên. Mở file xong thì overlay có thể trống vài trường.

→ Không phải lỗi, chỉ là chưa có dữ liệu. Bấm play một lần.

## 6. Nguyên tắc đã giữ xuyên suốt

- **Không encode video** (anh Trí chốt). Phần cắt copy stream.
- **Không suy ID từ thứ tự frame hay từ thời gian.** Frame thứ 3459 trong file mang ID 3460.
- **CSV mới copy nguyên byte từng dòng**, không parse rồi format lại.
- **Chỉ dùng API có từ Qt 5.12 / GStreamer 1.16.**
- **Comment trong code bằng tiếng Anh.**
- **Mỗi `.cpp` chỉ include header của chính nó.**

## 7. Câu hỏi cần hỏi anh Trí

| Câu hỏi | Vì sao | Trạng thái |
|---|---|---|
| Dòng CSV của frame bị rớt (3459, 3461) bỏ hay giữ? | Hồ sơ cũ nêu vậy, mặc định đang chọn **bỏ** | **Chưa hỏi** |
| Cắt video có cần hỗ trợ MP4 không? | Code hiện chỉ TS | **Chưa hỏi** |
| Có cần cho người dùng thấy đầu đoạn thực tế sau khi cắt không? | Stream copy bắt đầu ở keyframe, lệch có thể tới 10 s | **Chưa hỏi** |
| Đồ thị pan/tilt có cần bấm để tua không? | Hồ sơ cũ có nêu | **Chưa hỏi** |
