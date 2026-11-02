# Bàn giao Mục 10 và Mục 11

Tài liệu này mô tả phần code đã sinh sau tài liệu bàn giao đầu tiên.
Toàn bộ code nằm trong Git của `video_metadata_player`, mỗi thay đổi
một commit.

## Danh sách commit

| Commit | Nội dung |
|---|---|
| `6852c33` | Mục lục keyframe (`KeyframeIndex`, `NalParser`) |
| `ff6d808` | Dùng mục lục cho seek chính xác (gate `WAIT_UNTIL_ENTRY`) |
| `c64d589` | Vẽ hình bằng texture của scene graph thay vì QPainter |
| `f07a419` | Overlay thông số kỹ thuật trên hình |
| `ab6ee85` | Cắt video không làm thay đổi hình |
| `5e9bd49` | Đồ thị pan / tilt |
| `0a4dacd` | Thời lượng ổn định trên file không có index |

## Mục 10.1 — Mục lục keyframe

### Cách hoạt động

`KeyframeIndex` đọc file một lần bằng pipeline
`filesrc ! parsebin ! fakesink`, không giải mã, chỉ đọc NAL unit để tìm
điểm vào. Kết quả được công bố sau mỗi keyframe, nên phần đã quét là
dùng được ngay.

Mục lục được ghi vào file cache `<video>.vmpidx` (magic `VMPIDX1`).
Lần chạy sau dùng file cache, nên khởi động tức thì. File cache được
kiểm tra bằng kích thước và thời gian sửa của video, nên video đổi thì
mục lục được dựng lại. Nếu thư mục của video không ghi được, cache nằm
trong `QStandardPaths::CacheLocation`.

### Hai mốc thời gian

Mục lục lưu hai giá trị cho mỗi keyframe:

- **Stream time** (ms): thời gian người dùng thấy. Dùng để tìm keyframe
  trước một mục tiêu seek.
- **PTS thô** (ns): thời điểm gốc trong file. `tsdemux` giữ nguyên PTS
  của file và chỉ dịch mốc bằng segment (file TS bắt đầu ở 3600 s nhưng
  hiển thị từ 125 ms). Vì vậy gate so sánh bằng PTS thô.

Đây là điểm quan trọng nhất của phần này: nếu so sánh bằng stream time
thì gate sẽ chờ một keyframe đã đi qua.

### Kết nối vào gate

`VideoPlayer::updateEntryPoint()` tra mục lục trước mỗi seek chính xác,
ghi PTS thô vào `m_entryPts` (atomic). `FLUSH_STOP` chuyển gate sang
chế độ `WAIT_UNTIL_ENTRY`. Từ đó gate **loại bỏ mọi hình trước** keyframe
đích, nên decoder không phải giải mã những hình sẽ bị vứt đi.

Seek xem trước (giữ chuột kéo thanh trượt) vẫn dùng `WAIT_IDR` như cũ,
vì seek xem trước cần một hình sạch, không cần đúng vị trí.

### Đo lại

`VideoPlayer` đếm số buffer đi vào decoder (`decoderBuffers`).
Overlay hiện số này, và có hộp **Use index** để tắt/bật để so sánh.

Kỳ vọng trên file TS 10 s GOP: số buffer mỗi seek chính xác giảm từ
khoảng 642 xuống khoảng 117 (chỉ một GOP).

### Công cụ đo

```
vmp_tools/keyframe_test/keyframe_test.pro     -> keyframeIndexTool
vmp_tools/keyframe_test/count_indexed.pro      -> countIndexed
```

- `keyframeIndexTool <video> [expected]` — in mục lục, thời gian quét,
  thời lượng chính xác, khoảng cách lớn nhất giữa hai keyframe. So với
  `ffprobe` để kiểm tra số keyframe.
- `countIndexed <video>` — đo số buffer vào decoder cho 8 seek chính xác,
  lần đầu tắt index, lần sau bật index. In ra số trung bình.

## Mục 10.2 — Vẽ hình bằng texture

`FrameView` chuyển từ `QQuickPaintedItem` sang `QQuickItem` với
`updatePaintNode()`. Hình được đưa lên màn hình bằng `QSGTexture`, việc
thu nhỏ do card đồ họa làm nên không tốn thời gian CPU.

So sánh ảnh bằng `QImage::cacheKey()`, không so sánh từng pixel — so
sánh pixel mỗi hình sẽ đọc 1280×720×4 byte, quá đắt.

Nền đen chuyển thành một `Rectangle` riêng trong QML, vì item không tự
vẽ nền nữa.

## Mục 10.3 — Thời lượng ổn định

Ba thay đổi:

1. Thời lượng chỉ **tăng**, không giảm. Ước lượng của demuxer TS thay đổi
   liên tục và thường ngắn lại, làm tay nắm thanh trượt trượt.
2. Khi mục lục đang quét, thời gian nó đã đi tới là cận dưới tốt hơn
   cuối hình đã xem. Khi quét xong, mục lục cho thời lượng chính xác.
3. Vị trí của pipeline có thể vượt cuối file, được giới hạn lại theo
   thời lượng.

## Mục 11.3 — Overlay thông số

Bấm phím **O** để bật/tắt. Overlay ở góc trên trái, hiện:

```
ID      0001234
PTS     00:01.234
PAN     12.5
TILT    -3.1
MODE    PTZ
SPEED   1x
CODEC   H.264
SIZE    1920 x 1080
FPS     25.000
DEC BUF 8421
```

`DEC BUF` là số buffer đã đi vào decoder — cho thấy seek tốn bao nhiêu.

Tên codec do thread streaming thương lượng, nên nó được ghi vào chuỗi
có mutex và GUI thread được thông báo bằng queued call.

## Mục 11.4 — Cắt video

Phím **I** đánh dấu đầu, **U** đánh dấu cuối, nút **Cut** để cắt.

Cách làm: **copy stream, không encode lại**. File mới giữ nguyên từng
hình, nên barcode vẫn đọc được và metadata vẫn tra được.

Vấn đề: stream chỉ bắt đầu được ở keyframe, nên file mới luôn chứa
thêm vài hình trước hình đã đánh dấu. Để biết chính xác hình nào thuộc
về đoạn cắt, cutter đọc barcode của **mọi** hình trong file mới, rồi
dùng đúng các id đó để lọc dòng CSV. File CSV của đoạn cắt vì thế
đúng bằng các dòng của các hình đã đánh dấu, không phải do đoán.

File sinh ra: `<video>_cut.mp4` và `<video>_cut.csv` (thêm số thứ tự nếu
đã tồn tại). Công việc chạy ở thread pool nên giao diện không bị treo.

Lưu ý khi test: pipeline copy dùng `tsdemux ! h264parse ! mpegtsmux`,
nên **file nguồn phải là MPEG-TS**. File MP4 sẽ không copy được (cần
thêm nhánh `qtdemux ! mp4mux`). Nếu cần MP4 thì báo em thêm.

## Mục 11.5 — Đồ thị pan / tilt

Đồ thị vẽ ở phía trên panel phải, hiện pan (xanh) và tilt (cam).
Vạch dọc trắng đánh dấu dòng CSV của hình đang hiện. Hình có giá trị
không phải số thì để khoảng trống trong đường.

## Thứ tự test ngày mai

1. **Build**: `qmake && mingw32-make` (hoặc `nmake` với MSVC). Kiểm tra
   lỗi include — máy này không có Qt nên chưa build được.
2. **Mục lục**:
   - Chạy `keyframeIndexTool` trên file mẫu, so số keyframe với
     `ffprobe -select_streams v -show_frames -show_entries frame=key_frame`.
   - Chạy `countIndexed`, xem số buffer mỗi seek có giảm không khi bật
     index.
3. **Mục lục trong giao diện**: mở file TS, xem dòng "Indexing xx%" chạy
   tới 100%. Mở lại lần hai phải tức thì (đọc file cache).
4. **Seek**: bấm chuột kéo thanh trượt rồi thả, đo thời gian tới khi
   hình dừng. So sánh trước / sau khi bật "Use index".
5. **Thời lượng**: xem thanh trượt có giữ nguyên chiều dài khi seek hay
   không.
6. **Overlay**: bấm O, kiểm tra codec / kích thước / fps đúng với
   `ffprobe`.
7. **Cắt**: bấm I ở một hình, U ở hình sau đó, bấm Cut. Mở file mới,
   kiểm tra hình đầu tiên và barcode của các hình. Mở CSV mới, so với
   các dòng có id trong khoảng đã đánh dấu.
8. **Đồ thị**: kiểm tra đường pan / tilt có khớp với cột pan / tilt
   của CSV không.
9. **Hiệu năng**: đo CPU khi phát (mục 10.2), và khi kéo thanh trượt
   (mục 10.1).

## Những chỗ còn rủi ro

- **Chưa build được ở máy này.** Chỉ có kiểm tra tĩnh: cân bằng ngoặc,
  độ dài dòng, quy tắc include, và đối chiếu khai báo với định nghĩa.
  Lần build đầu anh có thể gặp lỗi kiểu thiếu include hoặc sai chữ ký.
- **`copyStream` của Mục 11.4** dùng `gst_element_query_position` để
  biết đã tới cuối đoạn cắt. Với TS, vị trí này có thể không chính
  xác bằng thời gian thật. Nếu file cắt bị cắt sớm hoặc muộn, cần
  đổi sang đếm hình qua một nhánh appsink.
- **`WAIT_UNTIL_ENTRY`** dùng dung sai 2 ms. Nếu hình của decoder có PTS
  hơi khác PTS của bộ phân tích, gate có thể bỏ sót hình đích. Khi
  đó tăng dung sai lên 10–20 ms, hoặc so sánh bằng stream time kèm
  mốc offset lấy từ index.
- **Cắt video chưa hỗ trợ MP4** (xem trên).
- **`PanTiltGraph`** vẽ bằng QPainter. Chấp nhận được vì nó chỉ vẽ lại
  khi bảng CSV mới tải hoặc khi hình đổi, nhưng nếu sau này nó vẽ mỗi
  hình thì phải chuyển sang texture như `FrameView`.
