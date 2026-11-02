# 01 — Quy ước và kiến trúc

## 1. Quy ước team (bắt buộc)

Tóm tắt từ hồ sơ cũ, mục 1. Toàn bộ code mới trong lần bàn giao này đã theo đúng.

- **C++11** (không C++14/17), **Qt 5.12.7 + qmake**, **QML**, **GStreamer 1.16**.
- Chỉ dùng API có trong Qt 5.12 và GStreamer 1.16. Ví dụ: **không** có `GST_SEEK_FLAG_INSTANT_RATE_CHANGE` (cần 1.18).

### Quy tắc file

- Header đuôi `.hpp`, không dùng `.h`.
- Tên file = tên class, chữ đầu viết thường: `videoPlayer.hpp` ↔ `class VideoPlayer`.
- File `.cpp` **chỉ** include header của chính nó (trừ `main.cpp`). Mọi include khác nằm trong `.hpp`.

### Quy tắc đặt tên

| Loại | Quy ước | Ví dụ |
|---|---|---|
| Tham số vào | `_name` | `void seek( qint64 _positionMs, bool _accurate )` |
| Tham số ra | `name_` | `void readIds( ..., int &countOut_ )` |
| Member | `m_name` | `qint64 m_position` |
| Hằng số | `ALL_CAPS` | `static const int MAX_GATE_DROPS` |

### Quy tắc hình dạng

- Dấu ngoặc kiểu **Allman**, luôn có `{}`.
- `if ( a == 2 )` — **có khoảng trắng trong ngoặc**.
- Thụt lề 4 dấu cách.
- **Tối đa 80 ký tự mỗi dòng.**
- Doc comment kiểu Qt: `/*! \brief ... */`.
- Không dùng `using namespace`.
- Ưu tiên `nullptr`, `auto`, RAII, smart pointer.
- **Comment trong code bằng tiếng Anh.**

### Kiểm tra tự động

Script `_lint.py` (ở máy viết code, chưa đưa vào repo) kiểm tra:
cân bằng ngoặc, độ dài dòng, tab/trailing whitespace, quy tắc include, guard header.

Thư mục `vmp_tools/` **cố ý bị bỏ qua** — đó là scratch code đo lường, không phải sản phẩm.

## 2. Kiến trúc sau khi thêm mục lục

```mục lục
                    ┌──────────────────────────────────────────┐
                    │            GUI THREAD (QML)               │
                    │  main.qml                                  │
                    │   ├─ VideoPlayer   (seek, position, props) │
                    │   ├─ MetadataStore (parse CSV, tra ID)     │
                    │   ├─ KeyframeIndex (đọc mục lục, tra key)  │
                    │   ├─ CutController (mark in/out, điều phối)│
                    │   └─ VideoCutter   (copy stream, đọc mã)   │
                    └──────────────────────────────────────────┘
                         │                    ▲
      seek / duration     │                    │ queued call
      (atomic)           ▼                    │
    ┌───────────────────────────────────────────────────────────┐
    │              GStreamer PIPELINE (playbin)                 │
    │  filesrc ! tsdemux ! h264parse ! avdec_h264              │
    │                                │                          │
    │                    ┌───────────┴────────────┐             │
    │                    │  DecoderGate (probe)   │             │
    │                    │  + đếm buffer (probe2) │             │
    │                    └───────────┬────────────┘             │
    │                                ▼                          │
    │                    videoconvert ! appsink                 │
    └───────────────────────────────────────────────────────────┘
                         │                    ▲
                         ▼                    │ queued call
    ┌───────────────────────────────────────────────────────────┐
    │   STREAMING THREADS (GStreamer tự quản lý)               │
    │   - giải mã                                            │
    │   - đọc mã vạch (BarcodeReader, 20–55 µs/frame)          │
    │   - chỉ giữ frame MỚI NHẤT (m_frameMutex)               │
    └───────────────────────────────────────────────────────────┘
                         │
                         ▼
    ┌───────────────────────────────────────────────────────────┐
    │   THREAD POOL (QtConcurrent)                               │
    │   - KeyframeIndex::scanFile  (quét file, KHÔNG decode)   │
    │   - VideoCutter::runCut      (copy stream + đọc mã vạch) │
    │   - MetadataStore::parseFile  (parse CSV)                 │
    └───────────────────────────────────────────────────────────┘
```

## 3. Bảng thread và việc của từng thread

| Thread | Ai tạo | Việc | Cách giao tiếp |
|---|---|---|---|
| **GUI** | `main()` | QML, nhập liệu, seek, cập nhật nhãn | — |
| **Streaming** (GStreamer) | `playbin` | giải mã, `storeFrame`, đọc mã vạch | appsink → biến `m_pendingFrame` dưới `m_frameMutex` |
| **Streaming** (probe) | GStreamer | gate: bỏ hình trước keyframe | atomic `m_entryPts`, `m_previewSeek` |
| **Bus** | GStreamer | EOS, ERROR, DURATION_CHANGED, ASYNC_DONE | `gst_bus_set_sync_handler` → `QMetaObject::invokeMethod(..., QueuedConnection)` + generation |
| **Pool** (quét index) | `QtConcurrent::run` | đọc file tìm keyframe | `QMetaObject::invokeMethod` → publish snapshot dưới `QMutex` |
| **Pool** (cắt video) | `QtConcurrent::run` | copy stream, đọc mã vạch file mới | `QMetaObject::invokeMethod` (progress) + `QFutureWatcher` (kết quả) |
| **Pool** (parse CSV) | `QtConcurrent::run` | đọc CSV, dựng bảng tra cứu | `QFutureWatcher` |

## 4. Các quyết định đa luồng và lý do

Đây là phần anh Trí có thể hỏi. Em giữ nguyên các quyết định của hồ sơ cũ và bổ sung phần mới.

### 4.1 Chỉ giữ frame MỚI NHẤT, không có hàng đợi

`m_pendingFrame` dưới `m_frameMutex`. Frame cũ chưa kịp lấy thì bị ghi đè.

**Lý do:** GUI chậm hơn decoder thì hàng đợi phình vô hạn, bộ nhớ tăng không kiểm soát. Video là dữ liệu cũ — bỏ frame cũ là đúng bản chất.

**Hệ quả:** nếu decode nhanh hơn GUI, hình trên màn hình có thể nhảy. Đó là đánh đổi có ý thức.

### 4.2 Bus message phải qua GUI thread

`gst_bus_set_sync_handler` **ref** message rồi `invokeMethod` với `Qt::QueuedConnection`, kèm **generation** của pipeline.

**Lý do:**
- Handler bus chạy ở thread của GStreamer, không phải GUI thread. Sửa property QML từ đó là lỗi.
- Generation bỏ kết quả của pipeline đã đóng — tránh "ma trận" khi mở file nhanh liên tiếp.

### 4.3 Chỉ một seek chạy tại một thời điểm

`m_seekInFlight` + `m_pendingSeekMs`. Request mới ghi đè request cũ.

**Lý do:** kéo thanh trượt tạo hàng chục request/giây. Nếu chạy song song, CPU vọt và hình nhảy loạn. Coalescing giữ đúng một seek đang chạy và một seek chờ.

### 4.4 Seek xem trước vs seek chính xác — hai chế độ gate khác nhau

| | Kéo thanh trượt (preview) | Thả tay (accurate) |
|---|---|---|
| Cờ GStreamer | `KEY_UNIT \| SNAP_NEAREST` | `ACCURATE \| FLUSH` |
| Gate | `WAIT_IDR` | `WAIT_UNTIL_ENTRY` (có mục lục) |
| `output-corrupt` | `FALSE` (không hiện hình hỏng) | `TRUE` (phải hạ cánh đúng) |
| Mục tiêu | thấy hình **nào đó** gần đó | hạ cánh **đúng frame** |

**Lý do tách hai chế độ:** seek xem trước cần một hình sạch (IDR thuần, không thiếu tham chiếu) để barcode đọc đúng ID — nếu hiện hình hỏng, barcode đọc ra ID sai, đó chính là lỗi 8.7 trong hồ sơ cũ. Seek chính xác thì phải hiện đúng frame, nên chấp nhận hình hơi hỏng.

### 4.5 Gate bỏ hình, không chặn pipeline

Gate là **pad probe** trên pad sink của decoder, trả về `GST_PAD_PROBE_DROP`.

**Lý do:** chặn (không trả về) sẽ làm pipeline đứng. Bỏ thì demuxer vẫn đọc file (rất rẻ, I/O tuần tự), chỉ decoder không phải làm việc thừa.

**Đây là điểm mấu chốt của cả mục 10.1**: giảm **CPU**, không giảm **I/O**.

### 4.6 Đường lui an toàn

- `MAX_GATE_DROPS = 600`: bỏ quá nhiều hình thì mở gate, không bao giờ chặn playback.
- `MAX_ACCURATE_RETRIES = 2`: seek hạ cánh lệch thì thử lại cùng mục tiêu (lỗi 8.9, 8.10 trong hồ sơ cũ).
- Không có mục lục → hành vi y như trước, không có đường lui nào mới.

### 4.7 Mục lục: snapshot bất biến + mutex ngắn

`QSharedPointer<const Snapshot>` đổi con trỏ dưới `QMutex`.

**Lý do:** reader lấy con trỏ (copy nhanh), rồi tra nhị phân **ngoài khóa**. Không giữ khóa khi tìm kiếm, vì gate đọc trên streaming thread và không được trễ.

**Generation để bỏ kết quả cũ:** mở file B khi file A đang quét → kết quả A không được publish.

### 4.8 Mục lục công bố từng phần, không chờ hết

Sau mỗi keyframe, `scanFile` báo kết quả qua callback → GUI publish.

**Lý do:** file 2 giờ mất 15–40 s quét (6,3 GB). Chờ hết thì seek đầu tiên 40 s sau khi mở file là rất tệ. Bản đồ đã quét dùng được ngay.

### 4.9 Cắt video chạy ở thread pool

`QtConcurrent::run` + `QFutureWatcher`.

**Lý do:** copy file 6 GB mất hàng chút giây, đọc barcode mọi hình còn lâu hơn. Chạy ở GUI thread sẽ treo giao diện.

### 4.10 Tiêu chí "CSV khớp 100%" là **đo**, không phải **tính**

Đây là quyết định quan trọng nhất của mục 11.4.

Stream copy **bắt đầu ở keyframe**, nên đầu đoạn không thể chính xác tới frame (hồ sơ cũ mục 3.5 đo được: yêu cầu 100–103 s → ra ID 2551–2579).

Nếu lọc CSV theo **thời gian người dùng nhập**, CSV sẽ lệch với video.

→ Đọc barcode của **chính file đã cắt**, lấy đúng danh sách ID có thật, lọc CSV theo đó.

Kết quả: **số frame video = số dòng CSV**, và frame thứ k mang ID bằng dòng thứ k.

## 5. So sánh mốc thời gian — phần quan trọng nhất của lần bàn giao này

### 5.1 Vì sao có hai mốc

File TS của máy công ty:

| Đại lượng | Giá trị |
|---|---|
| PTS thô của frame đầu | **3 600 s** (offset kiểu TS) |
| Stream time của frame đầu (cái người dùng thấy) | **125 ms** |
| Frame thứ 2 | +40 ms |

`tsdemux` giữ nguyên PTS gốc trong buffer, chỉ dịch mốc bằng **segment** (`gst_segment_to_stream_time`).

Hệ quả: **giá trị duy nhất mà bộ quét và decoder chắc chắn giống nhau là PTS thô.**

### 5.2 Gate so sánh thế nào

```
PTS_thô(hình) + dung_sai  <  PTS_thô(keyframe_đích)   →  bỏ hình
còn lại                                               →  cho qua
```

Dung sai mặc định: **2 ms**. Xem `08_loi_tien_mung.md` mục "Dung sai 2 ms".

### 5.3 Ba điều phải đúng

1. `KeyframeIndex` lưu **PTS thô** cùng stream time.
2. Gate so sánh bằng **PTS thô**.
3. Mọi đường lui phải **mở gate**, không được chặn.

Nếu một trong ba sai → seek chính xác không hạ cánh, hoặc hiện hình sai ID.

## 6. Những gì đã đổi so với hồ sơ cũ

| Thành phần | Trước | Sau |
|---|---|---|
| `FrameView` | `QQuickPaintedItem`, vẽ bằng CPU | `QQuickItem` + `QSGSimpleTextureNode`, GPU tự thu nhỏ |
| `videoPlayer.cpp` | 1 170 dòng | 1 238 dòng |
| `videoPlayer.hpp` | 340 dòng | 382 dòng |
| Số file `.cpp`/`.hpp` | 12 | 19 |
| Tổng dòng sản phẩm | 2 910 | **4 838** |
| Số commit | 5 | 10 |

## 7. Cấu trúc file sau khi dọn dẹp

```
videoplay/
├── README.md                     <- mô tả ngắn, cách build
├── .gitignore
├── videoMetadataPlayer.pro
├── qml.qrc
├── src/
│   ├── main.cpp
│   ├── player/
│   │   ├── videoPlayer.hpp / .cpp
│   │   └── frameView.hpp / .cpp
│   ├── index/
│   │   ├── nalParser.hpp / .cpp
│   │   └── keyframeIndex.hpp / .cpp
│   ├── metadata/
│   │   ├── barcodeReader.hpp / .cpp
│   │   ├── metadataTable.hpp
│   │   └── metadataStore.hpp / .cpp
│   ├── cutter/
│   │   ├── videoCutter.hpp / .cpp
│   │   └── cutController.hpp / .cpp
│   └── views/
│       └── panTiltGraph.hpp / .cpp
├── qml/main.qml
├── docs/                         <- 12 file .md bàn giao
│   ├── README.md
│   ├── 00_tong_quan.md
│   ├── 01_quy_uoc_va_kien_truc.md
│   ├── 02_muc_luc_keyframe.md
│   ├── 03_ve_bang_texture.md
│   ├── 04_thoi_luong_on_dinh.md
│   ├── 05_overlay_thong_so.md
│   ├── 06_cat_video.md
│   ├── 07_do_thi_pan_tilt.md
│   ├── 08_loi_tien_mung.md
│   ├── 09_keichung_chi_tiet.md
│   ├── 10_cong_cu_do.md
│   └── handoff_10_11.md
└── vmp_tools/                    <- công cụ đo
```

### 7.1 Vì sao tên file trong `docs/` có tiền tố số

Hai quy ước khác nhau là **cố ý**:

| | Quy ước | Vì sao |
|---|---|---|
| Code | tên file = tên class, chữ đầu thường | Chuan team (bắt buộc) |
| Tài liệu | tiền tố số theo thứ tự đọc | Đọc tuần tự, dễ tra |

Nếu đặt tài liệu theo tên class sẽ vừa lặp với code vừa mất thứ tự đọc.

### 7.2 Vì sao `docs/` nằm ngoài `src/`

`docs/` không phải code, không được compile, không thuộc `.pro`. Đặt cùng `src/` sẽ gây nhầm khi build lại toàn bộ.

### 7.3 Include path trong `.pro`

```qmake
INCLUDEPATH += \
    $$PWD/src \
    $$PWD/src/player \
    $$PWD/src/index \
    $$PWD/src/metadata \
    $$PWD/src/cutter \
    $$PWD/src/views
```

Nhờ vậy mỗi header include header của module khác bằng **đường dẫn ngắn**:

```cpp
#include "videoPlayer.hpp"     // trong src/player/
#include "nalParser.hpp"       // trong src/index/
```

→ Không phải viết `#include "../../index/nalParser.hpp"` — đường dẫn tương đối như thế là mùa và dễ vỡ khi di chuyển file.

### 7.4 Cách dùng path của qmake

| Ký hiệu | Ý nghĩa |
|---|---|
| `$$PWD` | Thư mục chứa file `.pro` |
| `$$VMP` | Biến do ta đặt (dùng trong `vmp_tools`) |
| `$$(...)` | Giá trị của biến môi trường |

`$$PWD` là cách **đúng** để tham chiếu thư mục project. Cách cũ `$$(HOME)/cuong_workspace/...` là đường dẫn tuyệt đối của máy — **không dùng cho sản phẩm**, chỉ dùng trong `vmp_tools` đã sửa lại thành `$$PWD` tương đối.

### 7.5 Quy tắc phụ thuộc giữa các module

| Module | Được biết | Không được biết |
|---|---|---|
| `player` | `index`, `metadata` | `cutter`, `views` |
| `index` | — | tất cả |
| `metadata` | — | tất cả |
| `cutter` | `player`, `metadata` | `index`, `views` |
| `views` | — | tất cả |

→ Cho phép viết một công cụ đo chỉ cần `index` mà không phải link cả `player`.

**Cách kiểm tra:** `vmp_tools/keyframe_test/keyframe_test.pro` chỉ link `nalParser` + `keyframeIndex`, không link `videoPlayer`. Nếu sau này `index` cần `player`, công cụ này sẽ phải sửa theo.
