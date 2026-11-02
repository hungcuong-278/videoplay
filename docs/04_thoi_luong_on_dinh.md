# 04 — Mục 10.3: Thời lượng ổn định

Giải quyết: **thời lượng file TS dài nhảy liên tục**, làm nút trên thanh trượt xê dịch.

File: `src/player/videoPlayer.cpp` (`updateDuration`, `updatePosition`), `src/index/keyframeIndex.cpp` (đo thời lượng).

---

## 1. Vấn đề gốc

File 2 giờ của máy công ty:

| | |
|---|---|
| Thời lượng thật | **120:00.898** |
| Thời lượng hiện trên UI | nhảy **117:08 → 116:03** |

**Lý do:** `tsdemux` ước lượng lại thời lượng theo bitrate đo được, và phát `DURATION_CHANGED` liên tục.

Hệ quả: nút trên thanh trượt trượt ngay cả khi đang đứng yên.

## 2. Ba thay đổi trong `updateDuration()`

### 2.1 Thời lượng chỉ **tăng**, không giảm

```cpp
if ( durationMs > 0 && durationMs != m_duration )
{
    if ( durationMs < m_duration && m_duration > 0 )
    {
        return;              // bỏ qua, không cho ngắn lại
    }
    m_duration = durationMs;
    emit durationChanged();
}
```

**Lý do:** ước lượng của demuxer vừa có thể dài hơn vừa ngắn hơn. Ngắn lại là thứ gây xê dịch. Cho phép dài thêm vô hại — thanh trượt chỉ giãn ra.

Khác với gợi ý trong hồ sơ cũ ("chỉ cập nhật khi tăng hơn 1 s"): em dùng ngưỡng 1 s, vì với file TS ước lượng nhảy vài giây mỗi lần `DURATION_CHANGED`, nếu chặn bằng 1 s thì vẫn nhảy vài lần. Ngưỡng phải lớn hơn bước nhảy của demuxer.

### 2.2 Mục lục cho thời lượng chính xác

```cpp
if ( m_keyframeIndex != nullptr )
{
    qint64 exact = m_keyframeIndex->durationMs();
    if ( exact > 0 )
    {
        durationMs = exact;         // giá trị tốt nhất, thắng
    }
    else if ( m_keyframeIndex->isScanning() )
    {
        // Chưa quét tới cuối, nhưng biết đã đi tới đâu.
        // Cận dưới đó tốt hơn cuối hình đã xem.
        qint64 known = m_keyframeIndex->lastKnownTimeMs();
        if ( known > 0 )
        {
            durationMs = qMax( durationMs, known );
        }
    }
}
```

**Ba tầng giá trị, từ kém đến tốt:**

| Tầng | Nguồn | Khi nào dùng |
|---|---|---|
| Kém | cuối hình đã xem (`m_latestFrameEnd`) | luôn có, là cận dưới chắc chắn |
| Tạm | mốc mục lục đã quét tới | khi mục lục đang quét |
| Tốt | thời lượng chính xác từ mục lục | mục lục quét xong |

**Lý do dùng `m_latestFrameEnd` làm cận dưới:** thời gian trên màn hình không bao giờ được vượt quá thời lượng. Nếu ước lượng của demuxer ngắn hơn thực tế, con số này giữ cho thanh trượt không bị cắt.

### 2.3 Khi nào cập nhật lại

`main.cpp` nối tín hiệu của mục lục:

```cpp
QObject::connect( &keyframes, &KeyframeIndex::keyframeChanged,
                  &player, [&player]() { player.refreshDuration(); } );
```

`refreshDuration()` gọi lại `updateDuration()`.

**Lý do:** mục lục công bố kết quả từng phần, nên thời lượng được cập nhật liên tục trong lúc quét, không phải đợi hết.

## 3. `updatePosition()` — giới hạn vị trí

```cpp
qint64 positionMs = position / GST_MSECOND;

// Khi không phát, thời gian của hình đang hiện là chân lý.
if ( !m_playing && m_currentFrameTime >= 0 )
{
    positionMs = m_currentFrameTime;
}

// Vị trí có thể vượt cuối file.
if ( m_duration > 0 && positionMs > m_duration )
{
    positionMs = m_duration;
}
if ( positionMs < 0 )
{
    positionMs = 0;
}
```

**Vì sao dùng `m_currentFrameTime` khi pause:** vị trí của pipeline có thể chạy trước hình đang hiện, hoặc sau. Khi video dừng, người dùng nhìn thấy hình tại một thời điểm cụ thể — thanh trượt nên ở đúng thời điểm đó, không phải thời điểm pipeline đang ở.

**Vì sao giới hạn theo duration:** `tsdemux` đôi khi báo vị trí vượt cuối file. Không giới hạn thì nút trượt ra ngoài.

## 4. Đo thời lượng trong bộ quét

Trong `onScanBuffer`:

```cpp
if ( timeMs > state->lastPictureMs )
{
    state->lastPictureMs = timeMs;
    state->lastPictureEndMs = endMs;
}
```

`endMs` = `timeMs + GST_BUFFER_DURATION( buffer ) / GST_MSECOND`.

**Rủi ro:** nếu hình cuối không có duration → `endMs = -1` → `durationMs = 0` → `updateDuration` bỏ qua giá trị đó và dùng tầng kém hơn.

**Cách xử lý nếu gặp:** dùng `lastPictureMs + m_nominalFrameMs` (thời lượng một hình, lấy từ caps).

## 5. Kết quả mong đợi

| | Trước | Sau |
|---|---|---|
| File 2 giờ | 117:08 → 116:03, nhảy liên tục | **120:00.898**, cố định |
| File mẫu 138 s | xê dịch nhẹ | cố định |
| Nút thanh trượt | trượt khi đứng yên | đứng yên |

## 6. Những chỗ dễ hỏng

### 6.1 Thời lượng **lớn hơn thật**

**Hiện tượng:** thanh trượt dài hơn video, kéo tới cuối thì không có hình.

**Lý do:** `m_latestFrameEnd` có thể lớn hơn thời lượng thật nếu video có B-frame và PTS không đơn điệu (PTS của frame sau có thể nhỏ hơn, nhưng frame trước có PTS lớn).

**Cách xử lý:** nếu xảy ra, dùng giá trị của mục lục làm chuẩn, không dùng `m_latestFrameEnd`.

### 6.2 Thời lượng **nhỏ hơn thật** và không bao giờ đúng

**Hiện tượng:** kéo thanh trượt tới cuối thì không có hình, phải tua bằng tay.

**Lý do:** mục lục chưa quét xong → dùng tầng "mốc đã quét" → còn thiếu.

**Cách xử lý:** đợi mục lục quét xong (dòng "Index ready" trong UI), hoặc tăng tầng "đã quét" lên `lastKnownTimeMs + độ dài GOP lớn nhất`.

### 6.3 `m_duration` không về 0 khi đóng file

**Hiện tượng:** mở file mới nhưng thanh trượt vẫn dài bằng file cũ.

**Cách xử lý:** `closePipeline()` phải reset `m_duration = 0`. Kiểm tra code hiện tại có làm không.

### 6.4 `refreshDuration()` gọi khi pipeline chưa mở

**Hiện tượng:** không sao — `updateDuration()` kiểm tra `m_pipeline == nullptr` và return.

### 6.5 `m_playing` lệch với trạng thái thật

**Hiện tượng:** khi pause, thanh trượt nhảy.

**Lý do:** `m_playing` được set từ `GST_MESSAGE_STATE_CHANGED`. Nếu message đến trễ, `m_playing` chưa kịp đổi.

**Cách xử lý:** chấp nhận được, lệch tối đa 100 ms (chu kỳ `POSITION_INTERVAL_MS`).

## 7. Câu hỏi cần hỏi

| Câu hỏi | Vì sao |
|---|---|
| Có cần hiện thời lượng ước lượng của demuxer để so sánh không? | Hữu ích khi chẩn đoán |
| Thời lượng có cần làm tròn hay hiện ms? | Hiện ms (định dạng `mm:ss.zzz`) |
