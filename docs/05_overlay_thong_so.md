# 05 — Mục 11.3: Overlay thông số kỹ thuật

Overlay đè lên hình video, hiện thông tin kỹ thuật của hình đang xem.

File: `qml/main.qml`, `src/player/videoPlayer.hpp/.cpp`, `src/index/nalParser.hpp/.cpp`.

---

## 1. Phím tắt

**`O`** — bật/tắt overlay.

Nằm trong `Shortcut` của `ApplicationWindow`, nên hoạt động kể cả khi focus ở ô nhập.

## 2. Nội dung hiển thị

```
ID      0001234
PTS     00:01.234
PAN     12.5
TILT    -3.1
MODE    PTZ
SPEED   1x
CODEC   H.264
SIZE    1280 x 720
FPS     25.000
DEC BUF 8421
```

| Dòng | Nguồn | Ghi chú |
|---|---|---|
| `ID` | `videoPlayer.frameId` | `---------` nếu hình không đọc được mã vạch |
| `PTS` | `videoPlayer.frameTime` | Thời gian của hình trên dòng thời gian của player |
| `PAN`/`TILT`/`MODE` | `row.pan` / `row.tilt` / `row.mode` | Từ CSV, `-` nếu hình không khớp dòng nào |
| `SPEED` | `videoPlayer.rate` | 0.25x – 4x |
| `CODEC` | `videoPlayer.videoInfo.codec` | "H.264" / "H.265" / "unknown" |
| `SIZE` | `videoPlayer.videoInfo.width/height` | Từ caps của appsink |
| `FPS` | `videoPlayer.videoInfo.fps` | 3 chữ số thập phân |
| `DEC BUF` | `videoPlayer.decoderBuffers` | **Số buffer đã đi vào decoder** |

### 2.1 `DEC BUF` là dòng quan trọng nhất

Đây là **thước đo** của mục 10.1: số buffer tới decoder cho thấy một lần seek tốn bao nhiêu việc.

Bật/tắt "Use index" và nhìn con số này thay đổi thế nào.

## 3. `DEC BUF` đếm thế nào

Probe thứ hai trên **cùng pad sink của decoder**, thêm **sau** probe của gate:

```cpp
gst_pad_add_probe( pad, GST_PAD_PROBE_TYPE_BUFFER,
                   &VideoPlayer::onDecoderBuffer, _data, nullptr );
```

**Thứ tự thực thi của các probe trên một pad là thứ tự thêm vào.** Nên:

- Probe 1 (gate) → có thể trả `GST_PAD_PROBE_DROP`
- Probe 2 (đếm) → chỉ thấy buffer **thật sự đi qua** gate

→ Số đếm là **việc decoder thật sự phải làm**, không phải số buffer demuxer đưa ra.

Đây là điểm khác biệt so với `decode_count/count.cpp` (tool cũ đếm buffer trước khi vào decoder).

## 4. `videoInfo` — thu thập thông tin video

### 4.1 Property

```cpp
Q_PROPERTY( QVariantMap videoInfo READ videoInfo NOTIFY videoInfoChanged )
```

`QVariantMap` thay vì nhiều property riêng, vì thông tin đếm được là 4 giá trị và sẽ tăng theo.

### 4.2 Kích thước và fps — từ caps của appsink

```cpp
GstPad *pad = gst_element_get_static_pad( m_appSink, "sink" );
GstCaps *caps = gst_pad_get_current_caps( pad );
GstVideoInfo videoInfo;
if ( gst_video_info_from_caps( &videoInfo, caps ) )
{
    info.insert( "width", GST_VIDEO_INFO_WIDTH( &videoInfo ) );
    info.insert( "height", GST_VIDEO_INFO_HEIGHT( &videoInfo ) );
    if ( GST_VIDEO_INFO_FPS_N( &videoInfo ) > 0 )
    {
        double fps = (double)GST_VIDEO_INFO_FPS_N( &videoInfo )
                     / GST_VIDEO_INFO_FPS_D( &videoInfo );
        info.insert( "fps", fps );
    }
}
```

**Lưu ý:** caps của appsink là **RAW** (`video/x-raw,format=RGBA`), nên **không có codec** trong đó. Codec phải lấy ở chỗ khác.

### 4.3 Codec — lấy ở chỗ thương lượng

`configureGate()` chạy ở **streaming thread**, lúc decoder thương lượng caps. Đó là chỗ duy nhất biết codec.

```cpp
int codec = NalParser::codecFromCaps( _caps );
QString name = QString::fromLatin1( NalParser::codecName( codec ) );
QMutexLocker locker( &m_codecMutex );
if ( m_videoCodec != name )
{
    m_videoCodec = name;
    QMetaObject::invokeMethod(
        this, [this]() { emit videoInfoChanged(); },
        Qt::QueuedConnection );
}
```

**Vì sao phải cẩn thận ở đây:**

`configureGate` chạy ở streaming thread. **Không được** `emit` signal có QML listener từ đó — sẽ chạy code QML ở sai thread.

→ Lưu vào chuỗi có mutex, rồi `invokeMethod` với `Qt::QueuedConnection` để GUI thread phát tín hiệu.

### 4.4 `NalParser::codecName`

```cpp
const char *NalParser::codecName( int _codec )
{
    switch ( _codec )
    {
    case CODEC_H264: return "H.264";
    case CODEC_H265: return "H.265";
    default:         return "unknown";
    }
}
```

Tách ra thành hàm để `NalParser` không phụ thuộc Qt — nó chỉ dùng GStreamer.

### 4.5 Khi nào cập nhật

`updateVideoInfo()` được gọi ở `GST_MESSAGE_ASYNC_DONE` — tức là sau khi pipeline preroll xong (lúc mở file và sau mỗi lần seek).

**Hệ quả:** overlay có thể trống vài trường ngay khi mở file. Bấm play một lần là hiện.

## 5. Giao diện

```qml
Rectangle {
    id: overlay
    anchors.left: parent.left
    anchors.top: parent.top
    anchors.margins: 10
    width: overlayText.implicitWidth + 20
    height: overlayText.implicitHeight + 14
    color: "#c0000000"
    radius: 4
    visible: root.showOverlay && videoPlayer.source !== ""

    Text {
        id: overlayText
        anchors.centerIn: parent
        color: "#e8f0ff"
        font.family: "monospace"
        font.pixelSize: 12
        lineHeight: 1.25
        text: { ... }
    }
}
```

**Vì sao dùng `Text` + `Rectangle` chứ không dùng `Label`:**
`Label` của Qt Quick Controls có padding, margin và style riêng. `Text` cho kiểm soát chính xác kích thước, và `implicitWidth`/`implicitHeight` cho khung bám sát nội dung.

**Vì sao nền là `#c0000000`:** 75% đen. Đủ để đọc, không che hình quá nhiều.

**Vì sao `font.family: "monospace"`:** con số phải thẳng cột để so sánh được.

## 6. Những chỗ dễ hỏng

### 6.1 Overlay trống lúc đầu

**Hiện tượng:** mở file, overlay hiện nhưng `CODEC`, `SIZE`, `FPS` đều `?`.

**Lý do:** `updateVideoInfo()` chỉ chạy ở `ASYNC_DONE` đầu tiên, mà đôi khi đến sau khi QML dựng xong.

**Cách xử lý:** không phải lỗi. Bấm play, hoặc tua một lần.

### 6.2 `SIZE` hiện `? x ?`

**Lý do:** caps của appsink chưa có (chưa renegotiate) → `gst_pad_get_current_caps` trả nullptr.

**Cách xử lý:** gọi `updateVideoInfo()` thêm ở `GST_MESSAGE_STATE_CHANGED` khi chuyển sang PLAYING.

### 6.3 `CODEC` hiện "unknown" với codec lạ

**Lý do:** `NalParser::codecFromCaps` chỉ nhận `video/x-h264` và `video/x-h265`. Codec khác (VP9, AV1, MPEG-4) → `CODEC_OTHER` → "unknown".

**Đây là hành vi đúng**, vì gate cũng chỉ xử lý được H.264/H.265 (xem hồ sơ cũ mục 8.8, 8.9).

**Cách xử lý nếu muốn đầy đủ:** đọc thẳng `GST_TAG_VIDEO_CODEC` từ caps, thay vì dùng `NalParser`.

### 6.4 `FPS` hiện `?`

**Lý do:** caps không có `framerate` (một số nguồn không khai báo).

**Cách xử lý:** tính từ `m_nominalFrameMs` (đã có sẵn từ `updateNominalFrameDuration`).

### 6.5 Overlay hiện đè lên Label "Open a video to start"

**Hiện tượng:** hai chữ chồng nhau.

**Lý do:** cả hai đều ở giữa / trên trái.

**Cách xử lý:** overlay đã có `visible: ... && videoPlayer.source !== ""`, nên khi chưa mở file thì không hiện. Nhưng nếu file lỗi, `source` khác rỗng → cả hai hiện. Kiểm tra `videoPlayer.errorString === ""`.

### 6.6 Overlay không cập nhật khi tua

**Hiện tượng:** `PTS` không đổi sau khi tua.

**Lý do:** QML binding `videoPlayer.frameTime` có NOTIFY `frameReady`, mà `frameReady` được emit mỗi hình → cập nhật. Nhưng nếu seek không tới đâu (hạ cánh sai), `frameTime` không đổi.

**Cách xử lý:** xem mục 10.1 và lỗi 8.9/8.10 trong hồ sơ cũ.

### 6.7 `row.found` sai sau khi đổi CSV

**Hiện tượng:** `PAN` hiện giá trị của frame cũ.

**Lý do:** `row` ở root là binding:

```qml
property var row: metadata.revision >= 0
                  ? metadata.rowInfo(videoPlayer.frameId) : ({})
```

`metadata.revision` tăng khi bảng mới tải xong → binding chạy lại ✅. Nhưng `rowInfo` là hàm không có tín hiệu phụ thuộc nào, nên QML không biết là phải gọi lại khi bảng đổi — chỉ khi `revision` đổi.

→ Nếu bảng đổi mà `revision` không tăng (trường hợp hiếm), `row` cũ. Kiểm tra `MetadataStore::onLoadFinished` có `++m_revision` ✅.

## 7. Việc có thể làm tiếp

Hồ sơ cũ mục 11.3 nêu "góc trên bên **phải**". Em đặt ở góc trên **trái**, vì thanh mã vạch ID nằm ở góc trên trái của hình — đặt overlay ở đó sẽ che mã vạch, mà mã vạch chính là thứ đang kiểm tra.

Nếu anh muốn đặt bên phải thì chỉ cần đổi `anchors.left` → `anchors.right`.

Ngoài ra có thể thêm: số thread của decoder, số buffer bị gate bỏ, thời gian quét mục lục, số keyframe.
