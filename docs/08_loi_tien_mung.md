# 08 — Sổ lỗi tiềm ẩn và rủi ro bất ngờ

Đây là **phần quan trọng nhất** của hồ sơ bàn giao.

Mỗi lỗi ở đây đều có: **hiện tượng** → **nguyên nhân gốc** → **cách xử lý** → **cách kiểm chứng**.

Mức độ:

| Mức | Nghĩa |
|---|---|
| **P0** | Chặn nghiệm thu — tính năng không dùng được |
| **P1** | Sai kết quả — anh sẽ thấy ngay |
| **P2** | Chỉ lộ ra khi test kỹ hoặc với file đặc biệt |
| **P3** | Hiếm, phải có điều kiện rất đặc biệt |

---

## Phần A — Lỗi đã biết trước, cần kiểm tra ngay khi build

### A1. `updatePaintNode` không được gọi → màn hình đen [P0]

**Hiện tượng:** mở file, không thấy hình nào, chỉ có nền đen.

**Nguyên nhân:** `FrameView` không có `setFlag( QQuickItem::HasContents, true )`.

**Cách xử lý:** thêm vào constructor của `FrameView`.

**Cách kiểm chứng:** mở file → phải thấy hình.

---

### A2. `m_window` / `window()` trả nullptr → không hiện hình [P1]

**Hiện tượng:** giống A1.

**Nguyên nhân:** `updatePaintNode()` được gọi trước khi item thuộc cửa sổ nào.

**Cách xử lý:** code đã kiểm tra `window() == nullptr` → trả về node rỗng, không crash. Nhưng cần chắc là không return sớm **trước** khi tạo node (node rỗng vẫn phải được trả về).

---

### A3. Dung sai 2 ms quá nhỏ → seek không hạ cánh [P1]

**Hiện tượng:** tua chính xác, hình đích không hiện; hoặc hiện hình trước đó.

**Nguyên nhân:** `m_entryToleranceNs = 2 * GST_MSECOND`. Nếu PTS của hình trong decoder lệch vài trăm µs so với PTS của bộ quét, hình đích bị bỏ.

**Nguyên nhân sâu hơn:** `h264parse` có thể chỉnh lại PTS. `tsdemux` đánh dấu discontinuity → đặt lại PTS. Hai bên không dùng cùng một nguồn timestamp.

**Cách xử lý, theo thứ tự ưu tiên:**

1. **Tăng dung sai** từ 2 ms lên 20 ms. Đo lại bằng `countIndexed` để chắc không bỏ quá nhiều.
2. **Dùng stream time kèm offset.** Mục lục biết stream time của keyframe; decoder biết stream time của hình qua `gst_segment_to_stream_time`. So sánh hai bên bằng cùng đơn vị ms → chính xác hơn, không cần dung sai.
3. **Lưu cả hai** vào `KeyframeIndex::Snapshot`: `times` (stream time) và `pts` (thô), rồi thử so sánh bằng stream time trước, raw PTS sau.

**Cách kiểm chứng:**

```bash
./vmp_tools/keyframe_test/keyframeIndexTool video.mp4
./vmp_tools/decode_count/count video.mp4
```

---

### A4. `m_entryPts` không được reset khi đóng file [P1]

**Hiện tượng:** đóng file A (có mục lục), mở file B (không có mục lục) → seek chính xác của B bị gate theo PTS của A → seek hỏng.

**Cách xử lý:** `closePipeline()` phải reset `m_entryPts = GST_CLOCK_TIME_NONE` và `m_entryToleranceNs = 0`.

**Trạng thái:** ✅ đã có (dòng 438–440 của `videoPlayer.cpp`).

---

### A5. `m_useKeyframeIndex` bật nhưng mục lục chưa quét tới vị trí đích [P1]

**Hiện tượng:** seek xa về cuối file, trong khi mục lục mới quét được 30% → seek không hạ cánh hoặc hạ cánh sai.

**Nguyên nhân:** `updateEntryPoint()` kiểm tra `_positionMs > lastKnownTimeMs()` → return. **Code đã có kiểm tra này** ✅. Nhưng nếu thiếu, đây là lỗi rất dễ mắc.

**Cách kiểm chứng:** mở file 2 giờ, tua ngay tới giữa file lúc mục lục còn đang quét.

---

### A6. `query_position` lệch → file cắt sai [P1]

Xem chi tiết ở `06_cat_video.md` mục 5.4 và 10.2/10.3.

**Tóm tắt:** với `tsdemux`, vị trí là ước lượng theo bitrate, lệch tới vài giây.

**Cách xử lý:** đếm hình qua `appsink` thay `query_position`.

---

### A7. `writeRows` bỏ qua ID không tìm thấy dòng → CSV lệch video [P0]

**Hiện tượng:** số dòng CSV ≠ số frame video. Vi phạm định nghĩa "khớp 100%".

**Nguyên nhân:** `if ( line.isEmpty() ) continue;` trong `writeRows`.

**Cách xử lý:** `writeRows` phải **báo lại** những ID không tìm thấy dòng, và `CutController` phải cảnh báo người dùng.

**Khi nào xảy ra:** ID trong video không có trong CSV (file 2 giờ có ID lặp, CSV gốc chỉ có 3 513 dòng).

---

### A8. Cảnh báo thiếu `#include` [P0]

**Hiện tượng:** lỗi compile.

**Nguyên nhân:** mỗi `.cpp` chỉ include header của chính nó, nên header phải include đủ.

**Cách xử lý:** xem bảng include bên dưới.

| Header | Phải có |
|---|---|
| `src/player/videoPlayer.hpp` | `<QObject> <QTimer> <QVariantMap> <QString> <QImage> <atomic> <memory>` + `gst/gst.h` |
| `src/player/frameView.hpp` | `<QQuickItem> <QImage>` + forward `QSGTexture` |
| `src/index/keyframeIndex.hpp` | `<QObject> <QVector> <QSharedPointer> <QMutex> <QString> <atomic>` |
| `src/index/nalParser.hpp` | `<QString>` + `gst/gst.h` |
| `src/cutter/videoCutter.hpp` | `<QObject> <QFutureWatcher> <QMutex> <QMutexLocker> <QString> <QStringList> <atomic> <functional> <memory>` + `barcodeReader.hpp` |
| `src/cutter/cutController.hpp` | `<QObject> <QString>` + `metadataStore.hpp`, `videoCutter.hpp`, `videoPlayer.hpp` |
| `src/views/panTiltGraph.hpp` | `<QQuickPaintedItem> <QColor> <QVector>` |
| `src/metadata/metadataStore.hpp` | `<QObject> <QHash> <QStringList> <QFutureWatcher>` + `metadataTable.hpp` |

**Lưu ý về đường dẫn include:** sau khi chia thư mục, các header của module khác được include bằng **đường dẫn ngắn** (`#include "videoPlayer.hpp"`), nhờ `INCLUDEPATH` trong `.pro` trỏ tới từng thư mục module.

→ Nếu build báo "file not found", kiểm tra `INCLUDEPATH` trong `videoMetadataPlayer.pro` có đủ 6 dòng không.

---

## Phần B — Lỗi đa luồng

### B1. Bus message xử lý ở sai thread [P0]

**Hiện tượng:** crash không giải thích được, hoặc QML báo lỗi property.

**Nguyên nhân:** `gst_bus_set_sync_handler` chạy ở thread GStreamer. Emit signal có QML listener từ đó → code QML chạy ở sai thread.

**Cách xử lý:** mọi message đều đi qua `QMetaObject::invokeMethod( ..., Qt::QueuedConnection )` với **generation** của pipeline.

**Cách kiểm chứng:** chạy `vmp_tools/seek_tsan/chaos.cpp` với ThreadSanitizer.

---

### B2. Generation cũ phủ đè generation mới [P1]

**Hiện tượng:** mở file B ngay sau file A → trạng thái của A ghi đè B (ví dụ `m_duration` của A đặt lên B).

**Nguyên nhân:** message của pipeline cũ tới sau message của pipeline mới.

**Cách xử lý:** mỗi message mang theo generation; handler so với `m_generation` hiện tại, lệch thì bỏ.

---

### B3. `configureGate` chạy ở streaming thread, đụng dữ liệu GUI [P1]

**Hiện tượng:** data race, giá trị `videoInfo.codec` bị nhảy.

**Nguyên nhân:** `configureGate()` được gọi từ pad probe (streaming thread), lưu codec vào `m_videoCodec`.

**Cách xử lý:** `m_codecMutex` bảo vệ, và **chỉ** `invokeMethod` với `QueuedConnection` để phát tín hiệu ở GUI thread.

**Kiểm chứng bằng TSan:**

```bash
g++ -fsanitize=thread -g chaos.cpp -o chaos-tsan
./chaos-tsan video.mp4
```

---

### B4. `m_frameMutex` không bảo vệ đủ [P0]

**Hiện tượng:** hình nhảy, hoặc crash trong `QImage` shared data.

**Nguyên nhân:** `m_pendingFrame` được ghi ở streaming thread, đọc ở GUI thread.

**Cách xử lý:** `QMutex m_frameMutex` bảo vệ cả hai. `QImage` copy-on-write nên copy rẻ.

---

### B5. Đọc `m_image` ở `updatePaintNode` — có phải race không? [P2]

**Phân tích:** `m_image` chỉ được **ghi** ở render thread (`updatePaintNode`) và **đọc** ở render thread. GUI thread (`setPlayer`) không chạm `m_image`.

→ **Không có race.** Nhưng nếu sau này thêm code GUI đụng `m_image` thì phải thêm khoá.

**Ghi chú cho người đọc code:** comment này phải nằm ngay cạnh `m_image` để người sau không vô tình phá.

---

### B6. `QFutureWatcher` gọi callback vào object đã bị xoá [P1]

**Hiện tượng:** crash khi đóng app khi đang cắt video.

**Nguyên nhân:** `QFutureWatcher` là member của `VideoCutter`; nếu `VideoCutter` bị xoá khi future còn chạy → callback vào con trỏ đã free.

**Cách xử lý:** `~VideoCutter()` phải set cancel flag, **đợi** future kết thúc (`m_watcher.waitForFinished()`), rồi mới cho đối tượng bị xoá.

**Kiểm chứng:** bấm Export rồi đóng app ngay → không được crash.

---

### B7. `m_cancelFlag` dùng chung giữa các lần chạy [P2]

**Hiện tượng:** hủy lần cắt này nhưng lần sau cũng bị hủy.

**Nguyên nhân:** tái dùng `std::shared_ptr<std::atomic<bool>>` cũ.

**Cách xử lý:** tạo `CancelFlag` **mới** mỗi lần `start()`.

---

### B8. `Snapshot` bị publish trong khi đang sửa [P1]

**Hiện tượng:** đọc mục lục ra giá trị rác, crash.

**Nguyên nhân:** `Snapshot` phải là **bất biến**. Nếu sửa trực tiếp object đang được chia sẻ → race.

**Cách xử lý:** luôn tạo `QSharedPointer<const Snapshot>` **mới**, điền đầy đủ, rồi đổi con trỏ dưới mutex.

---

### B9. `KeyframeIndex` chạy tiếp sau khi đóng file [P2]

**Hiện tượng:** mở file B xong, mục lục của A vẫn được publish → mục lục sai cho B.

**Cách xử lý:** `m_generation` trong `KeyframeIndex` đánh dấu lần quét; publish chỉ khi generation khớp.

---

## Phần C — Lỗi về thời gian và timestamp

### C1. So sánh stream time thay vì raw PTS [P0 — nguy hiểm nhất]

**Hiện tượng:** seek chính xác **không bao giờ hạ cánh**.

**Nguyên nhân:** file TS bắt đầu ở PTS 3600 s nhưng stream time là 125 ms. Nếu gate so sánh buffer stream time (125 ms) với entry stream time thì… thực ra hai bên cùng stream time thì vẫn khớp. Vấn đề là **bộ quét và decoder không chắc chắn cùng cách tính stream time**:
- Bộ quét dùng `gst_segment_to_stream_time` với segment của `parsebin`.
- Decoder dùng segment của `tsdemux` trong `playbin`.
- Hai segment có thể có **origin khác nhau**.

→ **Chỉ raw PTS mới là giá trị mà cả hai chắc chắn giống nhau.**

**Cách xử lý:** gate so sánh `GST_BUFFER_PTS` với `m_entryPts` (raw PTS). **Code đã làm đúng** ✅.

**Cách kiểm chứng:** xem lại đoạn `WAIT_UNTIL_ENTRY` — nếu thấy `gst_segment_to_stream_time` trong gate thì đã phá.

---

### C2. PTS không hợp lệ (`GST_CLOCK_TIME_NONE`) [P1]

**Hiện tượng:** gate không bao giờ bỏ hình → hành vi y như cũ (không hỏng, nhưng không tối ưu).

**Cách xử lý:** code kiểm tra `GST_BUFFER_PTS_IS_VALID( buffer )` ✅.

---

### C3. `m_duration` lớn hơn thật do B-frame [P2]

**Hiện tượng:** thanh trượt dài hơn video; kéo tới cuối thì không có hình.

**Nguyên nhân:** `m_latestFrameEnd` lấy từ hình có PTS lớn nhất. Với B-frame, PTS không đơn điệu theo thứ tự giải mã.

**Cách xử lý:** ưu tiên giá trị thời lượng từ mục lục (chính xác hơn).

---

### C4. `m_duration` không về 0 khi đóng file [P1]

**Cách xử lý:** `closePipeline()` reset `m_duration = 0` ✅ (dòng 447).

---

### C5. Thời lượng từ mục lục = 0 vì hình cuối không có duration [P2]

**Hiện tượng:** thời lượng vẫn nhảy dù đã có mục lục.

**Nguyên nhân:** `GST_BUFFER_DURATION` của hình cuối có thể là `GST_CLOCK_TIME_NONE`.

**Cách xử lý:** dùng `lastPictureMs + m_nominalFrameMs`.

---

### C6. Mốc thời gian của TS lệch giữa `tsdemux` và `parsebin` [P2]

**Hiện tượng:** bộ quét và decoder cho cùng một hình nhưng khác timestamp.

**Nguyên nhân:** `parsebin` tự chọn demuxer và có thể tạo segment khác `tsdemux` trong `playbin`.

**Cách xử lý:** nếu gặp A3, dùng cách "so sánh stream time kèm offset lấy từ mục lục".

---

### C7. Vị trí của pipeline vượt cuối file [P2]

**Cách xử lý:** clamp theo `m_duration` ✅.

---

### C8. `m_playing` lệch với trạng thái thật [P3]

**Hiện tượng:** khi pause, thanh trượt nhảy một chút.

**Nguyên nhân:** `STATE_CHANGED` đến trễ.

**Mức độ:** lệch tối đa 100 ms (`POSITION_INTERVAL_MS`). Chấp nhận được.

---

## Phần D — Lỗi của mục lục keyframe

### D1. Số keyframe lệch với ffprobe [P1]

**Hiện tượng:** bộ quét thấy 25, ffprobe thấy 28.

**Nguyên nhân khả dĩ nhất:** file mẫu có **recovery point** (intra refresh). Recovery point là I-frame không phải IDR → `isEntryPoint` trả false → bị bỏ.

**Cách xử lý:** mở rộng `NalParser::isEntryPoint` để nhận cả recovery point (H.264 type 1 với `recovery point` trong SEI, hoặc type 3/5).

**Lưu ý:** recovery point KHÔNG đóng GOP, nên frame trước recovery point có thể vẫn tham chiếu tới frame trước nó → gate bỏ quá nhiều là đúng, nhưng hình có thể bị hỏng nhẹ.

**Cách kiểm chứng:**

```bash
ffprobe -v error -select_streams v:0 \
  -show_entries frame=key_frame -of csv=p=0 video.mp4 | grep -c '^1$'
./vmp_tools/keyframe_test/keyframeIndexTool video.mp4
```

---

### D2. Mục lục rỗng với codec không phải H.264/H.265 [P2]

**Hiện tượng:** `keyframeCount() == 0`.

**Nguyên nhân:** `NalParser::codecFromCaps` chỉ nhận H.264/H.265.

**Cách xử lý:** không cần mục lục cho codec khác (gate cũng không hỗ trợ). Nhưng nên báo cho người dùng biết "index not available for this codec" thay vì im lặng.

---

### D3. Cache `.vmpidx` cũ được dùng lại [P2]

**Hiện tượng:** đổi file nhưng mục lục không đổi.

**Cách xử lý:** cache lưu kích thước + mtime của video → không khớp thì bỏ cache.

**Rủi ro còn lại:** đổi file mà **giữ nguyên kích thước và mtime** → dùng nhầm cache. Rất hiếm.

---

### D4. Thư mục video không ghi được [P3]

**Cách xử lý:** fallback sang `QStandardPaths::CacheLocation`.

---

### D5. File bị đổi giữa lúc đang quét [P3]

**Mục lục trong bộ nhớ sai.** Chấp nhận được — anh đổi file là phải mở lại.

---

### D6. Quét file rất lớn tốn thời gian [P2]

**Hiện tượng:** file 6,3 GB mất 15–40 s.

**Cách xử lý:** chạy nền, công bố từng phần. **Đã làm** ✅.

**Rủi ro:** quét file trên ổ cứng cơ song song với phát video → cả hai chậm.

---

### D7. `parsebin` không tìm thấy parser cho codec lạ [P2]

**Cách xử lý:** báo lỗi, không crash.

---

## Phần E — Lỗi của phần cắt video

Xem chi tiết ở `06_cat_video.md` mục 10. Tóm tắt:

| Mã | Vấn đề | Mức |
|---|---|---|
| E1 | `query_position` lệch → file cắt sớm/muộn | P1 |
| E2 | File cắt rỗng (MP4 thật, `tsdemux` không seek được) | P1 |
| E3 | CSV rỗng (không đọc được mã vạch) | P1 |
| E4 | CSV lệch video (thiếu dòng cho ID nào đó) | P0 |
| E5 | Mất audio (pipeline không có nhánh audio) | P2 |
| E6 | Đường dẫn có ký tự đặc biệt → `filesrc location` fail | P2 |
| E7 | File TS nhiều program → seek fail | P2 |
| E8 | ID lặp (file 2 giờ) → `filterIds` giữ cả hai lần | P2 |
| E9 | Encoding CSV (BOM / UTF-8 vs Windows-1252) | P2 |
| E10 | Đóng app khi đang cắt → file dang dở còn trên đĩa | P2 |

### E11. `GST_SEEK_FLAG_KEY_UNIT` vô dụng với MPEG-TS [P1]

**Hiện tượng:** seek không dừng ở keyframe, hoặc seek fail.

**Nguyên nhân:** `KEY_UNIT` chỉ có ý nghĩa với container **có bảng chỉ mục** (MP4, MKV). `tsdemux` không có bảng chỉ mục nên không thể "nhảy tới keyframe".

**Cách xử lý:** với TS chỉ dùng `GST_SEEK_FLAG_ACCURATE`. Demuxer tự lùi về keyframe trước.

**Code hiện tại đã đúng** ✅ (dùng `ACCURATE`). Nhưng nếu ai đó "sửa cho đúng" thành `KEY_UNIT | SNAP_BEFORE` thì sẽ hỏng.

### E12. `appsink` bỏ hình → đếm sai số frame [P0]

**Hiện tượng:** CSV có ít dòng hơn số frame của video cắt.

**Nguyên nhân:** `g_object_set( sink, "drop", TRUE )` → appsink bỏ hình cũ khi hàng đợi đầy → **mất hình không đếm**.

**Code hiện tại đã đặt `drop = FALSE`** ✅. Nhưng đây là chỗ rất dễ sửa nhầm vì mặc định của `appsink` là `drop = TRUE` khi `max-buffers` khác 0.

**Kiểm chứng:** so `framesVerified` với số frame thật của file cắt.

### E13. `uridecodebin` không có pad video ngay từ đầu [P1]

**Hiện tượng:** `readIds` trả về danh sách rỗng, không báo lỗi.

**Nguyên nhân:** liên kết cứng `gst_element_link_many( demux, convert, sink )` → luôn fail vì pad video chưa tồn tại.

**Cách xử lý:** dùng tín hiệu `pad-added` để liên kết động.

**Code hiện tại đã đúng** ✅ (`onDecoderPadAdded`).

### E14. `gst_filename_to_uri` trả nullptr [P2]

**Hiện tượng:** copy stream fail với thông báo khó hiểu.

**Nguyên nhân:** đường dẫn không hợp lệ (ký tự lạ, quá dài).

**Cách xử lý:** kiểm tra `nullptr` và báo lỗi rõ ràng. Code hiện tại ✅.

### E15. File đầu ra có đuôi `.mp4` nhưng nội dung là MPEG-TS [P2]

**Hiện tượng:** người dùng tưởng file là MP4, mở bằng công cụ khác thì báo sai định dạng.

**Nguyên nhân:** `copyStream` dùng `mpegtsmux` nhưng `runCut` đặt đuôi `.mp4`.

**Bối cảnh:** file mẫu của team cũng vậy — đuôi `.mp4` nhưng thật ra là TS. Nên đuôi `.mp4` **không sai** so với quy ước của team.

**Cách xử lý:** xem `06_cat_video.md` mục 9.2. Có thể giữ nguyên (nhất quán với file gốc) hoặc đổi thành `.ts`.

### E16. `cutStartMs` trả về giá trị yêu cầu, không phải giá trị thật [P1]

**Hiện tượng:** UI hiện "đầu đoạn thực tế" nhưng thực ra là điểm người dùng đánh dấu.

**Nguyên nhân:** `copyStream` gán `cutStartMs_ = _startMs`.

**Cách xử lý:** đọc PTS của buffer đầu tiên sau seek, hoặc tra mục lục.

**Đây là việc còn dở** — hồ sơ cũ yêu cầu hiện đầu đoạn thực tế.

---

## Phần F — Lỗi của đồ thị pan/tilt

| Mã | Vấn đề | Mức | Xử lý |
|---|---|---|---|
| F1 | Đồ thị trống (CSV chưa tải xong) | P2 | Chờ `metadata.loaded` |
| F2 | Đồ thị lệch với hình đang hiện | — | **Không thể xảy ra** — trục X là chỉ số dòng |
| F3 | Vẽ ra ngoài khung (giá trị hiện tại ngoài min/max) | P2 | Clamp |
| F4 | Vẽ lại toàn bộ mỗi frame → tốn CPU | P2 | Cache vào pixmap |
| F5 | File 2 giờ: ID lặp → đồ thị lặp | P3 | Chấp nhận (file đo) |
| F6 | `pan`/`tilt` không phải số → hiện 0 thay vì lỗ hổng | P2 | Dùng `toDouble(&ok)` |

### F7. `QByteArray::toDouble()` trả 0.0 khi thất bại, không trả NaN [P2]

**Hiện tượng:** dòng không có dữ liệu hiện giá trị 0 → đồ thị vẽ một đường thẳng về 0 → tưởng là dữ liệu thật.

**Nguyên nhân:** `QByteArray::toDouble()` không có tham số `ok` thì trả **0.0** khi parse lỗi. Chỉ khi truyền `&ok` mới biết là lỗi.

**Cách xử lý:** dùng `toDouble(&ok)` và đặt `qQNaN()` khi `!ok`.

**Kiểm chứng:** xem lại `MetadataStore::panTiltValues()` — nếu nó gọi `toDouble()` không có `ok` thì dòng lỗi sẽ hiện 0 thay vì lỗ hổng. **Đây là điểm cần sửa nếu anh muốn đồ thị đúng.**

### F8. NaN làm hỏng `valueRange` [P1]

**Hiện tượng:** toàn bộ đồ thị vẽ sai.

**Nguyên nhân:** `qMin( low_, NaN )` cho kết quả NaN → mọi phép tính toán phía sau thành NaN.

**Cách xử lý:** bỏ qua NaN khi tính min/max. Code hiện tại ✅.

### F9. Vẽ thẳng qua vùng NaN [P2]

**Hiện tượng:** đường đi thẳng qua chỗ không có dữ liệu.

**Cách xử lý:** nâng bút (`penDown = false`) khi gặp NaN. Code hiện tại ✅.

### F10. `setValues` gọi hai lần mỗi lần đổi bảng [P3]

**Hiện tượng:** vẽ lại đồ thị 2 lần thay vì 1.

**Nguyên nhân:** QML gọi `setValues` trong cả `onPanValuesChanged` và `onTiltValuesChanged`.

**Cách xử lý:** so sánh trước khi gán trong `setValues`. Code hiện tại ✅ — nhưng lưu ý `QVector<double>` so sánh là **O(n)**, với 3 513 phần tử thì vẫn nhanh hơn nhiều so với vẽ lại.

---

## Phần G — Lỗi QML và giao diện

### G1. QML báo lỗi tên property [P0]

**Hiện tượng:** `TypeError: Cannot read property 'xyz' of undefined`.

**Nguyên nhân:** đổi tên property trong C++ mà quên đổi QML.

**Cách kiểm chứng:** chạy và đọc console. Qt 5.12 báo rõ tên property sai.

---

### G2. `row` binding không cập nhật khi bảng đổi [P1]

**Hiện tượng:** overlay hiện pan/tilt của frame cũ sau khi nạp CSV mới.

**Nguyên nhân:** `metadata.rowInfo(id)` là hàm thuần, QML không biết phải gọi lại khi bảng đổi.

**Cách xử lý:** binding phải phụ thuộc vào `metadata.revision`:

```qml
property var row: metadata.revision >= 0
                  ? metadata.rowInfo(videoPlayer.frameId) : ({})
```

`revision` tăng khi bảng mới tải xong → binding chạy lại ✅.

---

### G3. Overlay che mã vạch [P2]

Đã tránh bằng cách đặt overlay ở góc trên **trái**? **Không** — mã vạch cũng ở góc trên trái.

**Cách xử lý nếu cần:** đặt overlay ở góc trên **phải** (hồ sơ cũ đề xuất), hoặc cho người dùng tắt overlay khi đang kiểm tra mã vạch.

---

### G4. Nút "Export" vẫn bấm được khi đang cắt [P2]

**Cách xử lý:** `enabled: !cutting`.

---

### G5. Thanh trượt không vẽ đoạn đã chọn [P3]

**Nguyên nhân:** binding sai, hoặc `markInMs >= markOutMs`.

---

## Phần H — Lỗi hiệu năng

### H1. CPU vẫn cao khi tua [P1]

**Hiện tượng:** sau khi bật "Use index", CPU vẫn 300%+.

**Nguyên nhân khả dĩ:**

1. Mục lục chưa quét tới vị trí đích → không dùng được.
2. PTS lệch → gate bỏ quá ít (xem A3).
3. `avdec_h264` dùng 8 thread, vẫn hết tốc lực trên phần việc còn lại.

**Cách kiểm chứng:** dùng `DEC BUF` trong overlay — nếu con số không giảm thì mục lục không dùng được.

---

### H2. CPU cao khi phát [P2]

**Nguyên nhân:** máy dùng OpenGL giả lập (`llvmpipe`).

**Kiểm tra:** `QSG_INFO=1` xem renderer.

**Lưu ý:** `QT_QUICK_BACKEND=software` **không** giải quyết được — backend software vẫn thu nhỏ trên CPU.

---

### H3. Đồ thị vẽ lại mỗi frame [P2]

Xem F4.

---

### H4. Bộ đọc mã vạch chạy trên streaming thread [P2]

**Hiện tượng:** tuần tự giải mã bị chậm hơn.

**Nguyên nhân:** `BarcodeReader` chạy trong callback của appsink → trên streaming thread.

**Đánh giá:** 20–55 µs mỗi frame × 25 fps = 1,4 ms/s. **Không đáng kể.** Không cần tách thread.

---

## Phần I — Lỗi build và môi trường

### I1. `qmake` trỏ tới Qt 5.12.8 của apt, không phải 5.12.7 [P0]

**Cách xử lý:** dùng đường dẫn đầy đủ:

```bash
/home/trungnd233/Qt5.12.7/5.12.7/gcc_64/bin/qmake
```

### I2. Thiếu `gstreamer1.0-plugins-*` [P1]

**Hiện tượng:** `No such element or plugin: tsdemux` / `avdec_h264`.

### I3. `playbin` không có trong GStreamer 1.16 [P2]

`playbin` nằm trong `gstreamer1.0-plugins-base`. Đã có trong danh sách cài đặt.

### I4. API có từ GStreamer 1.18 trở lên [P1]

**Cụ thể:** `GST_SEEK_FLAG_INSTANT_RATE_CHANGE` — **không dùng**.

**Kiểm tra:** `grep -rn "GST_SEEK_FLAG" *.cpp *.hpp` → mọi cờ phải có trong 1.16.

### I5. API có từ Qt 5.13 trở lên [P1]

**Ví dụ:** `QString::split` với `Qt::SplitBehavior` (5.14), `QVector::removeFirst` (5.13+).

### I6. `QSGSimpleTextureNode::setOwnsTexture` không có trong 5.12 [P2]

**Cách xử lý:** nếu lỗi compile, dùng `node->setTexture(texture)` và tự giữ texture trong member, xoá texture cũ khi thay.

### I7. `QVariantMap` với `Q_PROPERTY` và `NOTIFY` [P2]

**Cách xử lý:** tín hiệu phải được emit khi map thay đổi. Kiểm tra `updateVideoInfo()` có emit không.

---

## Phần J — Bảng ưu tiên xử lý

| Ưu tiên | Mã lỗi | Mức | Cần kiểm tra |
|---|---|---|---|
| 1 | A1 | P0 | Ngay khi build |
| 2 | A7, E4, E12 | P0 | Khi test cắt video |
| 3 | A3 | P1 | Khi test seek chính xác |
| 4 | D1 | P1 | Khi test mục lục |
| 5 | A6, E1, E16 | P1 | Khi test cắt video |
| 6 | H1 | P1 | Khi test hiệu năng |
| 7 | B1, B3 | P0 | Khi chạy TSan |
| 8 | F7 | P2 | Khi test đồ thị |
| 9 | E5, E9, E15 | P2 | Khi test cắt video thật |

## Phần J.1 — Lỗi đã được code hiện tại xử lý sẵn

Ghi lại để **không ai vô tình phá** khi sửa code:

| Mã | Chỗ xử lý |
|---|---|
| A1 | `FrameView` constructor có `setFlag( QQuickItem::HasContents, true )` |
| A4 | `closePipeline()` reset `m_entryPts` và `m_entryToleranceNs` |
| A5 | `updateEntryPoint()` kiểm tra `_positionMs > lastKnownTimeMs()` |
| C1 | Gate so sánh `GST_BUFFER_PTS`, **không** dùng stream time |
| C2 | Kiểm tra `GST_BUFFER_PTS_IS_VALID` |
| C4 | `closePipeline()` reset `m_duration = 0` |
| C7 | `updatePosition()` clamp theo `m_duration` |
| E11 | `copyStream` dùng `GST_SEEK_FLAG_ACCURATE`, không `KEY_UNIT` |
| E12 | `appsink` đặt `drop = FALSE` |
| E13 | `readIds` liên kết động qua `pad-added` |
| E14 | Kiểm tra `gst_filename_to_uri` trả nullptr |
| F8 | `valueRange` bỏ qua NaN |
| F9 | `paintLine` nâng bút khi gặp NaN |
| F10 | `setValues` so sánh trước khi gán |

---

## Phần K — Checklist trước khi trình bày

### K1. Build sạch

```bash
rm -rf build && mkdir build && cd build
qmake ../videoMetadataPlayer.pro && make -j4 2>&1 | tee build.log
grep -iE "error|warning" build.log
```

Không được có `error`. `warning` về unused variable là được chấp nhận nhưng nên dọn.

### K2. Không còn file media trong git

```bash
git ls-files | grep -iE "\.(mp4|ts|mkv|avi|csv|vmpidx)$"
```

Phải ra **rỗng**.

### K3. Không còn dòng quá 80 ký tự trong code sản phẩm

```bash
for f in $(git ls-files '*.cpp' '*.hpp'); do
  awk -v F="$f" 'length($0)>80 {print F": "NR": "length($0)}' "$f"
done
```

Phải ra **rỗng**.

### K4. Mỗi `.cpp` chỉ include header của chính nó

```bash
for f in $(git ls-files '*.cpp'); do
  h=$(basename "$f" .cpp)
  n=$(grep -c '#include "' "$f")
  echo "$f -> $n include(s)"
done
```

Mỗi file phải có đúng 1 include dạng `#include "..."`, và file đó phải là header của nó (trừ `main.cpp`).

### K5. Test đầy đủ

Theo `09_keichung_chi_tiet.md`.

### K6. Đo lại hiệu năng

Ghi kết quả thật vào bảng trong `09_keichung_chi_tiet.md`, thay số "kỳ vọng".
