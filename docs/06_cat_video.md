# 06 — Mục 11.4: Cắt video không encode

Giải quyết: **cắt video + CSV mới khớp 100%**.

File: `src/cutter/videoCutter.hpp` (222 dòng), `src/cutter/videoCutter.cpp` (579 dòng), `src/cutter/cutController.hpp` (116), `src/cutter/cutController.cpp` (265), `src/metadata/metadataStore.cpp` (`writeRows`).

---

## 1. Vấn đề gốc, đã đo chứ không đoán

Hồ sơ cũ mục 3.5 đo thử cắt video:

| Cách | Kết quả |
|---|---|
| Copy stream, yêu cầu 100–103 s | ra ID **2551–2579** (27 frame), lệch đầu đoạn |
| Copy stream, yêu cầu 70–75 s | ra ID 1761–1879 |
| Copy stream, yêu cầu 40–44 s | **file rỗng** |
| Encode lại x264 CRF 18/23/28 | mã vạch đọc được 100/100 |
| Encode lại CRF 35 | mất 4/100 frame |

**Kết luận:** không encode thì **không cắt đúng tới từng frame được**, vì đầu đoạn phải là keyframe.

Hệ quả: CSV phải được **đo** từ file đã cắt, **không được tính** từ thời gian người dùng nhập.

## 2. Nguyên tắc thiết kế

> **Số frame của video mới = số dòng của CSV mới.**
>
> Frame thứ k của video mới mang ID bằng ID ở dòng k của CSV mới, với mọi k.
>
> Mỗi dòng CSV mới giống hệt từng byte dòng tương ứng của CSV gốc.

Pipeline gồm 4 bước:

```
1. Mark In / Mark Out      → chốt đoạn, biết ID đầu và ID cuối
2. copyStream              → copy stream, không encode
3. readIds(file mới)       → đọc mã vạch từng hình của file mới
4. filterIds + writeRows   → lọc CSV gốc theo ID thật, copy nguyên byte
```

**Bước 3 là bước then chốt.** Nếu bỏ nó, CSV và video sẽ lệch.

## 3. Giao diện

### 3.1 Phím tắt và nút

| Việc | Cách |
|---|---|
| Mark In | nút "Mark In" hoặc phím `I` |
| Mark Out | nút "Mark Out" hoặc phím `U` |
| Bắt đầu cắt | nút "Export" |

### 3.2 Thanh trượt

Đoạn đã chọn được vẽ trên thanh trượt.

### 3.3 Thông tin hiện ra

```
IN  0002551  00:01:40.120
OUT 0002579  00:01:43.160
```

Sau khi cắt:

```
Cut written: 29 frames
```

## 4. `CutController` — điều phối

### 4.1 Property

| Property | Ý nghĩa |
|---|---|
| `markInMs` / `markOutMs` | Vị trí đã chốt (ms) |
| `markInId` / `markOutId` | ID của hình tại vị trí đó |
| `hasMarkIn` / `hasMarkOut` | Đã chốt chưa |
| `isRunning` | Đang cắt |
| `progress` | 0–100 |
| `statusText` | Mô tả bước hiện tại |
| `resultVideoPath` / `resultCsvPath` | Đường dẫn file đã tạo |
| `keptFrames` | Số dòng CSV đã ghi |
| `errorString` | Thông báo lỗi |

### 4.2 Vì sao cần cả `markInMs` và `markInId`

- `markInMs` → để cắt stream (seek tới đó).
- `markInId` → để lọc CSV.

**Vì sao không lấy ID từ thời gian:** ID trong video **không liên tục theo thời gian**. File mẫu: frame thứ 3459 mang ID 3460. Suy ID từ thời gian là sai.

### 4.3 Vì sao `markInId` lấy từ hình đang hiện

Khi bấm `I`, hình đang hiện **là** hình tại vị trí đó. `videoPlayer.frameId()` là ID đã đọc từ mã vạch của hình đó.

→ Không cần suy ra gì cả. ID là **đo được**, ngay tại thời điểm bấm.

### 4.4 Bốn kiểm tra trước khi cắt

```cpp
if ( m_running )                    → "A cut is already running"
if ( !m_hasMarkIn || !m_hasMarkOut )→ "Both a mark in and a mark out are needed"
if ( m_player == nullptr || source.isEmpty() ) → "No video is open"
if ( m_markOutMs <= m_markInMs )    → "The mark out is before the mark in"
```

Ngoài ra `markIn()` và `markOut()` tự kiểm tra:

```cpp
if ( frameId < 0 || frameTime < 0 )
    → "The frame on screen has no readable bar"
```

### 4.5 Xóa dấu khi đặt lại

- Đặt `markIn` mới → nếu `markOut <= markIn` thì **xoá markOut** (vì khoảng sẽ chạy ngược).
- Đặt `markOut` mới → nếu `markIn >= markOut` thì **xoá markIn**.

→ Khoảng luôn hợp lệ, không cần kiểm tra lại lúc bấm Export.

## 5. `VideoCutter::copyStream` — copy không encode

### 5.1 Pipeline

```
filesrc location=file://...
  ! tsdemux
  ! h264parse
  ! mpegtsmux
  ! filesink location=file://...
```

**Vì sao `h264parse`:** stream copy cần access unit gom đúng, và `mpegtsmux` cần `alignment=au`.

**Vì sao `mpegtsmux`:** giữ đúng loại container của file gốc (file mẫu là TS).

**Vì sao không dùng `playbin`:** `playbin` không cho kiểm soát seek chính xác và muxer ở mức pipeline tự tạo.

### 5.2 Đường dẫn → URI

```cpp
gchar *sourceUri = gst_filename_to_uri( _videoPath.toUtf8().constData(),
                                        &error );
```

**Bắt buộc phải dùng `gst_filename_to_uri`**, không dùng thẳng đường dẫn. Lý do: đường dẫn Windows có `:` (`C:\...`) → `filesrc location=` sẽ hiểu sai. `gst_filename_to_uri` sinh `file:///C:/...` đúng chuẩn.

**Phải kiểm tra `sourceUri == nullptr`** và báo lỗi — nếu không, `gst_element_factory_make` sẽ nhận URI rác.

### 5.3 Seek vào đầu đoạn

```cpp
gboolean seekOk = gst_element_seek(
    pipeline, 1.0, GST_FORMAT_TIME,
    GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET,
    _startMs * GST_MSECOND, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE );
```

**Điểm quan trọng:** seek được gửi tới **`pipeline`**, không phải tới `demuxer`.

Vì sao vẫn đúng: `gst_element_seek` trên pipeline gửi tới tất cả element có khả năng seek; `tsdemux` nhận và xử lý.

**Vì sao dùng `ACCURATE` chứ không phải `KEY_UNIT | SNAP_BEFORE`:**

`tsdemux` không hỗ trợ `KEY_UNIT` (nó không có bảng chỉ mục!). Cờ `KEY_UNIT` chỉ có ý nghĩa với container có index (MP4, MKV).

→ Với TS, chỉ có `ACCURATE` là dùng được, và demuxer **tự lùi về keyframe trước**.

**Đây chính là lý do đầu đoạn thực tế lệch trước điểm đánh dấu** — và cũng là lý do bước 3 (đọc barcode) là bắt buộc.

**Đường lui:** nếu seek thất bại → `qWarning() << "Cannot seek to" << _startMs << "ms, copying all"` → copy từ đầu file. Không crash.

### 5.4 Dừng ở cuối đoạn

`copyStream` theo dõi:

1. **Bus message `EOS`** → copy xong.
2. **`gst_element_query_position`** → khi `positionMs >= _endMs` → dừng.

```cpp
int percent = qBound( 0, static_cast<int>(
    60 * ( positionMs - _startMs )
        / qMax<qint64>( 1, _endMs - _startMs ) ), 60 );
```

→ Copy chiếm 60% tiến độ, đọc barcode chiếm 40%.

### 5.5 Vấn đề: `query_position` không đáng tin với TS

**Đây là lỗi tiềm ẩn tiền mùa.**

Với `tsdemux`, `gst_element_query_position` trả vị trí theo **bitrate ước lượng**, không phải PTS thật. Với GOP không đều (file mẫu có chỗ tới 10 s), vị trí này lệch tới **vài giây**.

| Hậu quả | Mức độ |
|---|---|
| File cắt bị cắt **sớm** (thiếu hình cuối) | Rõ ràng, anh sẽ thấy |
| File cắt bị cắt **muộn** (thừa hình) | Ít rõ, nhưng CSV vẫn khớp nhờ bước 3 |

**Cách xử lý nếu gặp:** đếm hình qua nhánh `appsink`, dừng khi hình hiện tại >= `_endMs`.

**Vì sao vẫn để nguyên:** đơn giản hơn, và **CSV luôn khớp** nhờ bước 3.

### 5.6 `cutStartMs` và `cutEndMs` hiện trả về giá trị **yêu cầu**

```cpp
cutStartMs_ = _startMs;
cutEndMs_ = _endMs;
```

**Không phải giá trị thực tế.** Điểm đầu thực tế là keyframe trước đó, nhưng code không biết nó.

→ Xem mục 9.3.

### 5.7 Đóng pipeline đúng cách

```cpp
gst_element_set_state( pipeline, GST_STATE_NULL );
gst_object_unref( bus );
gst_object_unref( pipeline );
```

**Phải đặt state NULL trước khi unref**, nếu không file đích có thể bị bỏ dở (index của TS chưa ghi xong).

### 5.8 Hủy giữa chừng

```cpp
if ( _cancel->load() )
{
    _errorString = "Cut cancelled";
    break;
}
```

→ Copy dừng. **Nhưng file đích vẫn còn** → xem mục 10.10.

## 6. `VideoCutter::readIds` — đọc mã vạch file mới

### 6.1 Pipeline

```
uridecodebin uri=file://...
  ! videoconvert
  ! appsink caps=video/x-raw,format=RGBA
```

**Vì sao dùng `uridecodebin` chứ không phải `tsdemux ! h264parse ! avdec_h264`:**

`uridecodebin` tự chọn demuxer **và** decoder theo nội dung file. Nhờ vậy:
- Mọi container đều chạy (TS, MP4, MKV).
- Mọi codec đều chạy (H.264, H.265, VP9...).
- **Không phải viết nhánh riêng cho từng codec** — đây là điểm tốt nhất của cách làm này.

**Đây là khác biệt so với `copyStream`**, dùng `tsdemux ! h264parse ! mpegtsmux` vì muxer phải biết trước loại container đầu ra.

### 6.2 Vì sao đọc mã vạch lại là cần thiết

Đây là điểm khác biệt lớn nhất so với cách làm thông thường.

| | Cách thông thường | Cách của em |
|---|---|---|
| Lọc CSV | theo thời gian người dùng nhập | theo ID **đọc từ file mới** |
| Độ chính xác | lệch tới đầu GOP (có thể 10 s) | **chính xác tuyệt đối** |
| Xử lý ID rớt | phải đoán | loại tự động |

**Ví dụ thật từ file mẫu:**

Người dùng đánh dấu đoạn 100–103 s. Copy stream bắt đầu ở keyframe trước đó (ID 2551). File mới chứa ID 2551–2579.

- Lọc CSV theo thời gian → lấy dòng của 100–103 s → **sai**.
- Lọc CSV theo ID thật (2551–2579) → **đúng**.

### 6.3 `BarcodeReader` dùng lại nguyên xi

Đã có sẵn từ commit 1–5, đọc đúng 3460/3460 frame, checksum đều hợp lệ, 0 ID sai, 20–55 µs mỗi frame.

**Vì sao dùng lại:** đã kiểm chứng, và cần đúng **cùng một** cách đọc mã vạch giữa player và cutter — nếu khác nhau thì ID không khớp.

### 6.4 Liên kết động

`uridecodebin` **không có** pad video ngay từ đầu — pad xuất hiện sau khi nó đọc được phần đầu file.

```cpp
g_signal_connect( demux, "pad-added",
                  G_CALLBACK( &onDecoderPadAdded ), pipeline );
```

`onDecoderPadAdded` liên kết `convert` vào pad đó.

**Đây là lỗi rất dễ mắc:** nếu liên kết cứng bằng `gst_element_link_many( demux, convert, sink )` thì sẽ **luôn fail** vì pad chưa có.

### 6.5 Thu thập ID qua `new-sample`

```cpp
g_object_set( sink, "emit-signals", TRUE, nullptr );
g_signal_connect( sink, "new-sample",
                  G_CALLBACK( &onVerifyFrame ), &state );
```

`IdState` giữ:

```cpp
QMutex mutex;
QStringList ids;
int count = 0;
CancelFlag cancel;
```

**Vì sao cần mutex:** callback `new-sample` chạy ở streaming thread, còn vòng lặp chính đọc `state.count` ở pool thread.

### 6.6 `appsink` không được bỏ hình

```cpp
g_object_set( sink, "max-buffers", 1, nullptr );
g_object_set( sink, "drop", FALSE, nullptr );
```

**`drop = FALSE` là bắt buộc.** Nếu `drop = TRUE`, appsink bỏ hình cũ khi hàng đợi đầy → **đếm sai số frame** → CSV lệch video.

Đây là điểm dễ mắc nhất của toàn bộ phần này.

**Vì sao `max-buffers = 1`:** chỉ cần giữ hình hiện tại, không cần hàng đợi.

### 6.7 `sync = FALSE`

```cpp
g_object_set( sink, "sync", FALSE, nullptr );
```

Không đồng bộ theo đồng hồ → chạy **nhanh nhất có thể** (đọc barcode 3 460 hình mất vài giây thay vì vài phút).

### 6.8 Chuyển `GstSample` thành `QImage`

```cpp
GstCaps *caps = gst_sample_get_caps( _sample );
GstVideoInfo info;
gst_video_info_from_caps( &info, caps );
GstVideoFrame frame;
gst_video_frame_map( &frame, &info, _buffer );
QImage image( GST_VIDEO_FRAME_PLANE_DATA( &frame, 0 ),
              GST_VIDEO_FRAME_PLANE_WIDTH( &frame, 0 ),
              GST_VIDEO_FRAME_PLANE_HEIGHT( &frame, 0 ),
              GST_VIDEO_FRAME_PLANE_STRIDE( &frame, 0 ),
              QImage::Format_RGB888 );
```

**Vì sao `Format_RGB888`:** barcode dựa trên **màu** (đen/trắng), cần đủ 3 kênh màu.

**Bắt buộc phải:**
- `gst_video_frame_map` trước khi dùng, `gst_video_frame_unmap` trong **mọi** đường thoát.
- `gst_sample_unref` sau khi dùng.
- `gst_caps_unref` sau khi dùng.

**Lỗi dễ mắc:** nếu `gst_video_frame_map` thất bại thì `frame.data[0]` là con trỏ rác → segfault.

### 6.9 `readIds` chạy ở pool thread

Không dùng `VideoPlayer` vì `VideoPlayer` cần GL context và GUI thread.

`uridecodebin` + `videoconvert` + `appsink` không cần GL → chạy ở bất kỳ thread nào.

## 7. `filterIds` — lọc ID

```cpp
QStringList VideoCutter::filterIds( const QStringList &_ids,
                                    qint64 _startId, qint64 _endId )
{
    QStringList result;
    if ( _startId < 0 || _endId < 0 )
    {
        // Without a marked range every id of the new file belongs to the
        // cut.
        return _ids;
    }
    for ( const QString &id : _ids )
    {
        bool ok = false;
        qint64 value = id.toLongLong( &ok );
        if ( !ok )
            continue;
        if ( value >= _startId && value <= _endId )
            result.append( id );
    }
    return result;
}
```

**Vì sao vẫn lọc theo ID đầu/cuối sau khi đã đọc barcode:**

Vì **đầu đoạn thực tế có thể sớm hơn điểm người dùng đánh dấu**. Copy stream bắt đầu ở keyframe trước → file mới chứa cả những hình trước điểm đánh dấu.

→ `filterIds` cắt bỏ đúng phần đó, và CSV khớp **100%** với những gì thực sự còn trong file.

**Đây chính là điểm khác biệt giữa "khớp 100%" và "khớp gần đúng".**

### 7.1 Trường hợp không có đánh dấu

`_startId < 0` → giữ **toàn bộ** ID đọc được. Đây là đường lui: nếu không đánh dấu được (hình không đọc được mã vạch), vẫn cắt được video, chỉ là CSV lấy hết.

### 7.2 Nếu ID trong video không đơn điệu

File 2 giờ có ID lặp lại 1 → 3462 sau mỗi 2:18. Nếu cắt đoạn dài > 2:18 thì `filterIds` giữ **cả hai lần** của mỗi ID.

**Đây là hành vi cần báo cho người dùng.** File 2 giờ chỉ dùng để đo, không phải để cắt thật.

## 8. `MetadataStore::writeRows` — ghi CSV mới

### 8.1 Nguyên tắc: copy nguyên byte

```cpp
file.write( m_table->headerLine );
file.write( "\n" );

for ( const QString &id : _ids )
{
    bool ok = false;
    qint64 value = id.toLongLong( &ok );
    if ( !ok ) continue;
    QHash<qint64, int>::const_iterator it =
        m_table->rowById.constFind( value );
    if ( it == m_table->rowById.constEnd() ) continue;
    // The row is kept byte for byte, so nothing of the metadata is
    // changed by the cut.
    file.write( m_table->lines.at( it.value() ) );
    file.write( "\n" );
    ++written;
}
```

**Vì sao copy nguyên dòng thay vì parse rồi format lại:**

1. **Chính xác** — CSV gốc có header không đều (`frameUlAmsl,frameUrLatitude`), số dạng `3.37794e+07`. Parse rồi format lại sẽ đổi mấy thứ.
2. **An toàn** — không cần biết hết 65 cột.
3. **Nhanh** — không parse 3 513 dòng lần nữa.

### 8.2 Index là `QHash<qint64, int>`

`m_table->rowById` map **ID (số)** → **chỉ số dòng**. Nên `writeRows` nhận `QStringList` (ID dạng chuỗi từ barcode) rồi đổi sang `qint64`.

### 8.3 Thứ tự dòng

`writeRows` ghi theo thứ tự của `_ids` (thứ tự đọc được từ file mới).

→ **Frame thứ k của video mới ↔ dòng thứ k của CSV mới.** Đúng như yêu cầu.

### 8.4 File rỗng thì **xoá**

```cpp
if ( written == 0 )
{
    file.close();
    QFile::remove( _targetPath );
    return false;
}
```

**Vì sao:** file CSV rỗng (chỉ có header) là kết quả sai — để lại sẽ gây nhầm lẫn. Xoá đi và báo lỗi.

### 8.5 Encoding

`m_table->lines` là `QByteArray` → giữ **nguyên byte** của file gốc, kể cả BOM. Header cũng vậy.

**Đây là ưu điểm lớn so với cách dùng `QString`**: byte BOM được giữ nguyên → Excel đọc đúng tiếng Việt.

## 9. `CutController` — kết nối với QML

### 9.1 Luồng

```
QML: bấm "Export"
  → cut.startCut()
     → kiểm tra điều kiện
     → sinh tên file đầu ra
     → VideoCutter::start( videoPath, targetPath,
                           markInMs, markOutMs, markInId, markOutId )
  → VideoCutter chạy ở pool thread
     → copyStream → readIds → filterIds
     → progress → tín hiệu → GUI
     → finished → CutController::onCutFinished()
        → MetadataStore::writeRows( csvPath, keptIds )
        → thông báo kết quả
```

### 9.2 Tên file đầu ra

```cpp
if ( !targetPath.endsWith( ".mp4" ) )
    targetPath += ".mp4";
int number = 1;
while ( QFileInfo::exists( targetPath ) )
{
    targetPath = QString( "%1_cut%2.mp4" ).arg( base ).arg( number );
    ++number;
}
```

**File đã tồn tại không bị ghi đè** — tự thêm số.

**Điểm đáng chú ý:** tên file đầu ra dùng đuôi **`.mp4`**, trong khi pipeline copy ghi bằng `mpegtsmux` → **nội dung là MPEG-TS nhưng đuôi là `.mp4`**.

**Đây là điểm cần quyết định:**

| Lựa chọn | Ưu | Nhược |
|---|---|---|
| Giữ `.mp4` | Giống file gốc (file mẫu đuôi `.mp4` nhưng thật là TS) | Người dùng khó nhận ra |
| Đổi thành `.ts` | Trung thực với nội dung | Khác file gốc |

**Cách xử lý nếu anh muốn đổi:** sửa `runCut()` — bỏ khối `if ( !targetPath.endsWith( ".mp4" ) )` và dùng đuôi theo container thật.

### 9.3 Phải báo cho người dùng biết đầu đoạn thực tế

Đây là yêu cầu trong hồ sơ cũ mục 11.4:

> "Cần nói thẳng: không encode thì đầu đoạn không thể chính xác tới frame... Nên hiện cho người dùng biết **đầu đoạn thực tế** sẽ là đâu trước khi xuất."

**Trạng thái hiện tại:** `VideoCutter` có `cutStartMs` / `cutEndMs`, nhưng chúng trả về **giá trị yêu cầu**, không phải giá trị thực tế (xem mục 5.6).

**Cách sửa để có giá trị thực tế:** trong `readIds`, sau khi đọc được ID đầu tiên của file mới, tra ngược trong mục lục để biết thời gian thật. Hoặc đơn giản hơn: `copyStream` đọc PTS của buffer đầu tiên sau seek.

**Hiện tại QML hiện:**

```
Cut written: 29 frames
```

→ **Chưa** hiện đầu đoạn thực tế. Đây là việc còn dở.

## 10. Những chỗ dễ hỏng

### 10.1 File cắt rỗng

**Hiện tượng:** file đầu ra có 0 byte.

**Lý do:**

1. `tsdemux` không tìm được điểm seek (file thật ra là MP4).
2. `query_position` báo vị trí sai ngay từ đầu → dừng ngay.
3. `_endMs <= _startMs` → đã bị chặn ở `start()`.

**Cách xử lý:** kiểm tra kích thước file sau khi copy. Nếu 0 → báo lỗi cụ thể.

### 10.2 File cắt bị cắt sớm

**Hiện tượng:** thiếu hình cuối.

**Lý do:** `query_position` lệch. Xem mục 5.5.

### 10.3 File cắt bị cắt muộn

**Hiện tượng:** có hình thừa ở cuối.

**Hậu quả:** **không nghiêm trọng** — CSV được lọc theo ID thật nên vẫn khớp. Nhưng video có hình thừa so với CSV → vi phạm định nghĩa "khớp 100%".

### 10.4 CSV mới rỗng

**Hiện tượng:** file CSV đầu ra không tồn tại (đã bị xoá) và báo lỗi.

**Lý do:** `written == 0`. Có thể vì `filterIds` lọc hết, hoặc `readIds` không tìm thấy mã vạch nào.

### 10.5 CSV mới lệch với video

**Hiện tượng:** số dòng CSV ≠ số frame video.

**Lý do:** `writeRows` bỏ qua ID không tìm thấy dòng (`if ( it == end ) continue;`).

**Hậu quả:** **nghiêm trọng** — vi phạm định nghĩa "khớp 100%".

**Cách xử lý:** `writeRows` phải **báo lại** những ID không tìm thấy dòng. Hiện tại **chưa làm** — hàm trả về `bool` thành công/thất bại, không báo chi tiết.

### 10.6 `rowById` không có ID đó

**Lý do:** CSV gốc có 3 513 dòng (ID 1→3513), video chỉ có 3 460 hình (ID 1→3462). ID 3463–3513 trong CSV không có hình.

Ngược lại, video 2 giờ có ID lặp → `rowById` giữ dòng **cuối cùng** có ID đó.

### 10.7 File video và CSV không cùng thư mục

**Cách xử lý:** `writeRows` nhận đường dẫn đầy đủ từ QML.

### 10.8 Đường dẫn có ký tự đặc biệt

**Đã xử lý** bằng `gst_filename_to_uri` ✅.

### 10.9 CSV gốc có BOM

**Đã xử lý**: giữ nguyên byte → BOM được giữ ✅.

### 10.10 File dang dở còn lại khi hủy

**Hiện tượng:** hủy giữa chừng → file cắt nửa vời vẫn nằm trên đĩa.

**Lý do:** `copyStream` dừng và đóng pipeline, nhưng không xoá file đích.

**Cách xử lý:** khi `_cancel->load()` → `QFile::remove( _targetPath )`.

### 10.11 Đóng app khi đang cắt

**Hiện tượng:** crash hoặc file dang dở còn lại.

**Cách xử lý:** `~VideoCutter()` phải set cancel và **đợi** future kết thúc (`m_watcher.waitForFinished()`).

### 10.12 File TS có audio

**Hiện tượng:** file cắt mất tiếng.

**Lý do:** pipeline copy chỉ nối `tsdemux ! h264parse ! mpegtsmux`. Nhánh audio bị bỏ qua.

**Cách xử lý:** nối thêm `aacparse` / `mpegaudioparse` tùy codec, hoặc dùng `parsebin` thay `h264parse` (parsebin tự chọn parser theo caps). **`parsebin` là cách tổng quát hơn.**

**Lưu ý:** `readIds` **không** bị ảnh hưởng — `uridecodebin` tự xử lý mọi codec.

### 10.13 File có nhiều program TS

**Hiện tượng:** seek không có tác dụng.

**Cách xử lý:** `tsdemux program-number=N` — nhưng N phải đúng, không đoán được.

### 10.14 `appsink` bỏ hình → đếm sai

**Xem mục 6.6.** Phải đặt `drop = FALSE`.

### 10.15 `uridecodebin` không có pad video

**Hiện tượng:** không đọc được hình nào, `ids` rỗng.

**Lý do:** liên kết cứng thay vì dùng `pad-added`.

**Cách xử lý:** dùng `pad-added` như code hiện tại ✅.

## 11. Việc còn dở nếu còn thời gian

| Việc | Ghi chú | Độ khó |
|---|---|---|
| Hiện **đầu đoạn thực tế** cho người dùng | Hồ sơ cũ yêu cầu rõ | Trung bình |
| Báo ID nào không tìm thấy dòng CSV | Sửa lỗi 10.5 | Thấp |
| Xoá file dang dở khi hủy | Sửa lỗi 10.10 | Thấp |
| Đếm hình qua `appsink` thay `query_position` | Sửa lỗi 10.2 | Trung bình |
| Hỗ trợ MP4 (`qtdemux ! parsebin ! qtmux`) | Máy công ty có `qtmux`/`mp4mux` | Thấp |
| Hỗ trợ audio | Dùng `parsebin` | Thấp |
| Đổi đuôi file đầu ra theo container thật | Xem mục 9.2 | Thấp |
| Hỏi anh Trí về dòng của frame bị rớt (3459, 3461) | Mặc định đang chọn bỏ | — |

## 12. Câu hỏi cần hỏi anh Trí

| Câu hỏi | Vì sao quan trọng |
|---|---|
| Dòng CSV của frame bị rớt (3459, 3461) bỏ hay giữ? | Quyết định định nghĩa "khớp 100%" |
| Cắt video có cần hỗ trợ MP4 không? | `copyStream` hiện chỉ TS |
| File TS có audio không cần giữ? | Ảnh hưởng pipeline copy |
| Đuôi file đầu ra nên là `.mp4` hay `.ts`? | Xem mục 9.2 |
| Có cần cho người dùng thấy đầu đoạn thực tế trước khi xuất? | Hồ sơ cũ đề xuất |
