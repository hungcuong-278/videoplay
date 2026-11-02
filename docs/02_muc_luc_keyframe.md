# 02 — Mục 10.1: Mục lục keyframe

Giải quyết: **CPU 400% mỗi lần tua chính xác trên file TS**.

File: `src/index/keyframeIndex.hpp` (260 dòng), `src/index/keyframeIndex.cpp` (763 dòng), `src/index/nalParser.hpp` (82), `src/index/nalParser.cpp` (173), và phần nối trong `src/player/videoPlayer.hpp/.cpp`.

---

## 1. Vấn đề gốc, đã đo chứ không đoán

| | MP4 / MKV | MPEG-TS |
|---|---|---|
| Bảng chỉ mục keyframe | Có | **Không** |
| Seek ACCURATE làm gì | nhảy thẳng tới keyframe trước mục tiêu | đoán vị trí byte theo bitrate, **lùi thêm khoảng an toàn**, rồi tìm keyframe đầu tiên |
| Số frame phải giải mã | **117** | **642** (có lần 901) |

Với GOP không đều (file mẫu có chỗ tới 10 s) và recovery point, điểm bắt đầu thường sớm hơn cần thiết **vài GOP**.

`avdec_h264` mặc định dùng số thread bằng số lõi (8 ở máy công ty), nên vài trăm frame đó bị giải mã **hết tốc lực trên mọi lõi** → `top` báo 400%.

Không phải rò rỉ, không phải vòng lặp bận: hết việc là CPU về 0.

## 2. Ý tưởng

Biết trước PTS của mọi IDR → khi tua tới T, **mọi buffer trước IDR cuối cùng ≤ T bị gate bỏ, không cho vào decoder**.

Demuxer vẫn đọc từ chỗ nó muốn (đọc file rất rẻ), nhưng decoder chỉ giải mã đúng phần cần — giống hệt MP4.

**Giảm CPU, không giảm I/O.**

## 3. `NalParser` — bộ đọc NAL dùng chung

Tách ra vì **hai chỗ cần đọc NAL theo đúng một cách**: bộ quét mục lục và gate của player. Nếu hai chỗ đọc khác nhau, mục lục và gate sẽ không thống nhất.

| Hàm | Việc |
|---|---|
| `codecFromCaps( GstCaps * )` | CODEC_H264 / CODEC_H265 / CODEC_OTHER |
| `codecName( int )` | "H.264" / "H.265" / "unknown" |
| `lengthSizeFromCaps( GstCaps * )` | 4 (avcC/hvcC) hoặc 0 (start code) |
| `pictureNalType( codec, lengthSize, buffer, limit )` | Loại NAL của hình đầu tiên trong buffer |
| `isEntryPoint( codec, type )` | H.264 type 5 (IDR), H.265 type 16–21 (IRAP) |
| `isRasl( codec, type )` | H.265 type 8–9 (RASL) — phải bỏ sau IRAP |

### 3.1 Vì sao phải biết start code hay length prefix

- **Byte stream** (MPEG-TS, elementary stream): NAL được tách bằng **start code** `00 00 01` hoặc `00 00 00 01`.
- **avc/hvc stream** (MP4, MKV): mỗi NAL có **tiền tố độ dài**, kích thước nằm trong codec data (`avcC` / `hvcC`).

Đọc sai thì ra toàn giả. `lengthSizeFromCaps` đọc `codec_data` (avcC: byte 4 & 0x03; hvcC: byte 21 & 0x03).

### 3.2 Vì sao `isEntryPoint` khác `isRasl`

Trong H.265, IRAP (BLA/IDR/CRA) mở đầu GOP, nhưng **RASL** (Random Access Skip Leading) là các hình *trước* IRAP mà vẫn tham chiếu hình của GOP trước. Sau khi tua, chúng không giải mã được.

→ Gate bỏ RASL sau khi gặp IRAP. Xem lỗi 8.8 trong hồ sơ cũ.

### 3.3 Vì sao giới hạn số byte đọc

`NAL_SCAN_LIMIT = 4096`. NAL header của hình luôn ở đầu access unit. Đọc 4 KB đầu là đủ, tránh đọc cả buffer 720p.

## 4. `KeyframeIndex` — quét nền

### 4.1 Pipeline quét

```
filesrc ! parsebin ! fakesink
```

**Không decode.** Chỉ demux + parse. Pad probe trên `parsebin` đọc NAL.

`parsebin` tự chọn demuxer, nên mọi container đều chạy, không cần biết trước là TS hay MP4.

### 4.2 Pad probe

`onScanBuffer`:
1. Đọc codec từ caps (`codecFromCaps`). Không phải H.264/H.265 → `GST_PAD_PROBE_DROP` (bỏ nhánh đó).
2. Lấy stream time: `gst_segment_to_stream_time( &segment, GST_FORMAT_TIME, PTS )`.
3. Lấy **PTS thô**: `GST_BUFFER_PTS( buffer )`.
4. Ứng viên keyframe: buffer **không** có cờ `GST_BUFFER_FLAG_DELTA_UNIT`.
   - Parser đánh dấu hình không phải keyframe bằng cờ delta → chỉ hình còn lại mới cần đọc NAL.
   - Đây là tối ưu quan trọng: file 2 giờ có hàng trăm nghìn hình, chỉ 28 hình là IDR.
5. Nếu ứng viên → đọc NAL → `isEntryPoint` → ghi vào danh sách.
6. Tiến độ: `GST_BUFFER_OFFSET( buffer )` / kích thước file. Không cần biết thời lượng.

`onScanEvent`: bắt `GST_EVENT_SEGMENT` để có mốc stream time.

### 4.3 Cấu trúc dữ liệu

```cpp
struct Snapshot
{
    QVector<qint64> times;      // stream time, ms
    QVector<qint64> pts;        // PTS thô, ns
    bool complete = false;
    QString errorString;
    qint64 durationMs = 0;      // thời lượng chính xác
    qint64 largestGapMs = 0;
};
```

`Snapshot` là **bất biến** (`const Snapshot`). Đổi con trỏ dưới `QMutex`, reader lấy con trỏ rồi đọc ngoài khóa.

### 4.4 Hai mốc thời gian — điểm mấu chốt

| | Dùng để | Vì sao |
|---|---|---|
| `times` (stream time, ms) | Tra mục lục: "keyframe nào ≤ T?" | Đây là thời gian người dùng thấy, cùng đơn vị với mục tiêu seek |
| `pts` (ns) | Gate so sánh | **Giá trị duy nhất** mà bộ quét và decoder chắc chắn giống nhau |

Xem `01_quy_uoc_va_kien_truc.md` mục 5 để hiểu vì sao.

### 4.5 Công bố từng phần

Sau mỗi keyframe, `scanFile` gọi callback → GUI thread publish snapshot mới.

**Lý do:** file 6,3 GB quét mất 15–40 s. Chờ hết thì seek đầu tiên phải chờ 40 s. Bản đồ đã quét dùng được ngay.

### 4.6 Hủy và chống kết quả cũ

- `m_cancelFlag` là `QSharedPointer<QAtomicInt>`. GUI thread set = 1 → pool thread thấy và dừng.
- `m_generation`: mở file B khi file A đang quét → generation tăng → kết quả A bị bỏ khi tới.

### 4.7 Cache file `.vmpidx`

Đặt cạnh video: `<video>.vmpidx`.

Nếu thư mục video không ghi được → `QStandardPaths::CacheLocation`.

**Định dạng:**

| Phần | Nội dung |
|---|---|
| Magic | `VMPIDX1` (8 byte) |
| `qint64` | kích thước file video |
| `qint64` | mtime của video (ms) |
| `qint64` | thời lượng chính xác (ms) |
| `qint32` | số keyframe |
| `qint64[]` | stream time của từng keyframe (ms) |
| `qint32` | số PTS (có thể 0 nếu cache cũ) |
| `qint64[]` | PTS thô của từng keyframe (ns) |

**Kiểm tra tính hợp lệ:** khác kích thước hoặc mtime → bỏ cache, quét lại.

**Tương thích ngược:** cache không có phần PTS thì vẫn đọc được; `entryPtsBefore` trả `false` → gate không dùng mục lục → hành vi y như cũ.

### 4.8 API công khai

| Hàm | Dùng ở đâu |
|---|---|
| `start( path )` / `stop()` | `main.cpp` |
| `isScanning()` / `progress()` | QML (thanh tiến độ) |
| `keyframeCount()` | QML |
| `isComplete()` | QML |
| `durationMs()` | `VideoPlayer::updateDuration()` |
| `largestGapMs()` | chẩn đoán |
| `lastKnownTimeMs()` | biết vùng nào đã quét |
| `entryTimeBefore( ms )` | tra nhị phân, dùng để test |
| `entryPtsBefore( ms, &ptsNs )` | **`VideoPlayer::updateEntryPoint()`** |
| `keyframeTimes()` | test |

## 5. Nối vào gate

### 5.1 Luồng

```
GUI thread                          Streaming thread
──────────                          ───────────────
seek(T, accurate)
  │
  ├─ updateEntryPoint(T, true)
  │    ├─ index->lastKnownTimeMs()
  │    │     └─ T > mốc đã quét → không dùng mục lục
  │    ├─ index->entryPtsBefore(T, &pts)
  │    │     └─ tra nhị phân trên times[], lấy pts[found]
  │    ├─ m_entryToleranceNs = 2 ms
  │    └─ m_entryPts = pts        ← atomic
  │
  └─ gst_element_seek(ACCURATE|FLUSH)
       │
       └─ FLUSH_STOP ──────────────→ gate:
                                       waitMode = WAIT_UNTIL_ENTRY
                                       gate->entryPts = m_entryPts
                                       gate->entryToleranceNs = ...
                                       │
                                       └─ buffer vào:
                                          PTS + tol < entryPts → DROP
                                          ngược lại → cho qua
```

### 5.2 `updateEntryPoint` — ba điều kiện phải đúng

```cpp
if ( !_accurate || !m_useKeyframeIndex || m_keyframeIndex == nullptr )
    return;                                  // không dùng mục lục

qint64 lastKnown = m_keyframeIndex->lastKnownTimeMs();
if ( lastKnown < 0 || _positionMs > lastKnown )
    return;                                  // chưa quét tới đó

qint64 ptsNs = -1;
if ( !m_keyframeIndex->entryPtsBefore( _positionMs, ptsNs ) )
    return;                                  // không có keyframe phù hợp
```

**Điều kiện 2 rất quan trọng:** nếu mục lục chưa quét tới vị trí đích, dùng PTS của keyframe đã biết sẽ **sai** — gate sẽ bỏ hình cho tới khi hết hạn.

### 5.3 Vì sao `m_entryPts` là atomic

`startSeek` chạy ở GUI thread, `onDecoderInput` chạy ở streaming thread. `std::atomic<guint64>` cho phép đọc/ghi không cần khóa.

`FLUSH_STOP` được xử lý dưới stream lock của pad, nên streaming thread **chắc chắn** thấy giá trị mới — nhưng vẫn phải là atomic vì `m_entryPts` còn được đọc ở chỗ khác.

### 5.4 Vì sao gate vẫn kiểm tra điểm hạ cánh

`onSeekDone` vẫn so `m_currentFrameTime` với `m_accurateTarget`, và thử lại tối đa 2 lần nếu lệch quá `MAX_LANDING_FRAMES`.

**Lý do:** mục lục có thể sai (xem mục 8), hoặc demuxer hạ cánh lệch (lỗi 8.9, 8.10 hồ sơ cũ). Cơ chế thử lại cũ vẫn cần.

### 5.5 Cờ bật/tắt

`VideoPlayer::useKeyframeIndex` (property `useKeyframeIndex` trong QML, hộp "Use index").

**Lý do:** nguyên tắc "đo trước, sửa sau". Không có đường lui thì không biết code mới có tốt hơn không.

### 5.6 Cờ "Preview" không dùng mục lục

`WAIT_IDR` giữ nguyên. Seek xem trước cần hình sạch, không cần đúng vị trí, và nó phải có tác dụng ngay cả khi mục lục chưa quét xong.

## 6. Thời lượng chính xác (lợi ích phụ của mục 10.1)

Bộ quét biết PTS của hình cuối cùng → thời lượng thật.

Nhưng: `lastPictureEndMs` lấy từ `GST_BUFFER_DURATION`. Nếu hình cuối không có duration → `endMs = -1` → `durationMs = 0` → không dùng.

**Kết quả mong đợi:** file 2 giờ, thời lượng đúng 120:00.898 thay vì nhảy 117:08 → 116:03.

Chi tiết ở `04_thoi_luong_on_dinh.md`.

## 7. Công cụ kiểm chứng

### 7.1 `keyframeIndexTool` — in mục lục

```bash
cd vmp_tools/keyframe_test
qmake keyframe_test.pro && make
./keyframeIndexTool /path/to/video.mp4 28
```

In ra: số keyframe, thời gian quét, thời lượng chính xác, khoảng cách lớn nhất, và từng keyframe.

So số keyframe với ffprobe:

```bash
ffprobe -v error -select_streams v:0 \
  -show_entries frame=key_frame -of csv=p=0 video.mp4 | grep -c '^1$'
```

### 7.2 `countIndexed` — đo hiệu quả

```bash
qmake count_indexed.pro && make
./countIndexed /path/to/long60.ts
```

Chạy 8 seek chính xác, **hai lần**: tắt index rồi bật index. In số buffer vào decoder mỗi lần.

Kết quả mong đợi:

| | Số buffer/seek |
|---|---|
| Không index | ~642 |
| Có index | ~117 |

## 8. Những chỗ dễ hỏng

### 8.1 PTS thô có thể không khớp PTS của decoder

**Hiện tượng:** seek hạ cánh nhưng hình đích không hiện, hoặc hiện hình trước đó.

**Lý do:** `h264parse` có thể chỉnh lại PTS (ví dụ `tsdemux` đánh dấu lại discontinuity).

**Cách xử lý:** tăng `m_entryToleranceNs` từ 2 ms lên 20 ms. Nếu vẫn sai, dùng cách chắc hơn: so sánh bằng **stream time** kèm mốc offset lấy từ mục lục.

### 8.2 Mục lục có keyframe nhưng decoder không dùng đúng keyframe đó

**Hiện tượng:** gate chờ mãi, hết `MAX_GATE_DROPS` thì mở gate → hành vi y như cũ.

**Cách xử lý:** không cần làm gì, đường lui đã xử lý. Nhưng nếu xảy ra thường xuyên thì mục lục của file đó có vấn đề.

### 8.3 File có recovery point nhưng không có IDR

Hồ sơ cũ mục 3.1 nói file mẫu có **intra refresh / recovery point**.

**Hiện tượng:** mục lục có ít keyframe hơn ffprobe.

**Lý do:** recovery point (non-IDR I-frame) là điểm vào hợp lệ nhưng **không phải IDR**.

**Cách xử lý:** nếu cần, mở rộng `NalParser::isEntryPoint` để nhận cả recovery point. Hiện tại em giữ đúng định nghĩa IDR/IRAP vì đó là thứ decoder chắc chắn giải mã được.

### 8.4 Mục lục lưu cạnh video không được

**Hiện tượng:** mở file lần hai vẫn quét lại.

**Cách xử lý:** `cachePath()` tự chuyển sang `QStandardPaths::CacheLocation`. Nếu vẫn không được (thư mục cache không ghi được), báo lỗi rõ ràng.

### 8.5 File bị đổi giữa lúc đang quét

**Hiện tượng:** mục lục sai.

**Cách xử lý:** cache kiểm tra size + mtime. Nhưng nếu file bị đổi **trong lúc quét**, mục lục trong bộ nhớ sai. → Chấp nhận được; anh đổi file là phải mở lại.

## 9. Câu hỏi cần hỏi

| Câu hỏi | Vì sao |
|---|---|
| Mục lục có cần nhận recovery point không? | File mẫu có intra refresh; nếu file nào đó chỉ có recovery point thì mục lục sẽ rỗng |
| Có cần hạn chế quét theo byte (ví dụ chỉ 4 GB đầu) không? | File rất dài sẽ quét lâu |
| Có cần giữ mục lục của file cũ trong RAM khi đổi file không? | Hiện tại đổi file là mất mục lục cũ |
