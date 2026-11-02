# 03 — Mục 10.2: Vẽ hình bằng texture của scene graph

Giải quyết: **hình khựng khi phát**, nghi do khâu hiển thị đang phóng ảnh bằng CPU.

File: `src/player/frameView.hpp` (68 dòng), `src/player/frameView.cpp` (99 dòng).

---

## 1. Vấn đề gốc

`FrameView` là `QQuickPaintedItem`. Mỗi frame, GUI thread:

1. Phóng 1280×720 RGBA vào kích thước cửa sổ bằng `QPainter` (SmoothPixmapTransform) — **trên CPU**.
2. Qt mới tải ảnh lên GPU.

Đo được **42–114% một lõi** chỉ để vẽ.

Ngoài ra, nếu máy đang vẽ bằng OpenGL giả lập (`llvmpipe`, hay gặp trong VirtualBox), mọi thứ chậm hơn nhiều.

### Kiểm tra máy đang vẽ bằng gì

```bash
QSG_INFO=1 ./build/videoMetadataPlayer video.mp4 2>&1 | grep -iE "renderer|vendor|version"
```

Thấy `llvmpipe` → OpenGL giả lập trên CPU.

## 2. Cách sửa

Viết lại `FrameView` từ `QQuickPaintedItem` thành `QQuickItem` có `updatePaintNode()`.

### 2.1 Vì sao đọc hình trong `updatePaintNode` được

`updatePaintNode()` chạy ở **render thread** của scene graph, trong **pha sync**, lúc GUI thread **đang bị chặn**.

→ Đọc `currentFrame()` ở đây **an toàn, không cần khóa thêm**. Frame chỉ đổi ở GUI thread, mà GUI thread đang bận.

Đây là lý do không dùng `QQuickPaintedItem` nữa: nó ép bạn vẽ trên GUI thread.

### 2.2 Ba bước

```cpp
// 1. Tạo texture từ ảnh (GPU nhận ảnh nguyên bản)
QSGTexture *texture = window()->createTextureFromImage( image );
texture->setFiltering( QSGTexture::Linear );

// 2. Node giữ texture, texture cũ tự bị xoá
node->setOwnsTexture( true );
node->setTexture( texture );

// 3. Hình chữ nhật letterbox, GPU tự thu nhỏ
node->setRect( pictureRect( image.size() ) );
```

**Kết quả:** GPU làm việc thu nhỏ, CPU chỉ còn copy ảnh một lần.

### 2.3 `pictureRect`

Giữ tỷ lệ khung hình, canh giữa trong item:

```cpp
QSizeF target = _pictureSize;
target.scale( width(), height(), Qt::KeepAspectRatio );
return QRectF( ( width() - target.width() ) / 2.0,
               ( height() - target.height() ) / 2.0,
               target.width(), target.height() );
```

### 2.4 So sánh ảnh bằng `cacheKey()`, không so sánh pixel

```cpp
if ( m_image.cacheKey() != image.cacheKey() )
```

**Vì sao:** `QImage::operator!=` so sánh **nội dung pixel**. Mỗi frame đó là đọc 1280×720×4 = 3,7 MB, tức ~3,7 MB × 25 frame/s ≈ 92 MB/s chỉ để so sánh. `cacheKey()` là con trỏ, O(1).

### 2.5 Nền đen chuyển ra QML

`QQuickPaintedItem` có `fillColor` (nền mặc định). `QQuickItem` thì không, nên QML phải tự vẽ:

```qml
Item {
    Rectangle { anchors.fill: parent; color: "black" }
    FrameView { anchors.fill: parent; player: videoPlayer }
}
```

### 2.6 `HasContents`

Bắt buộc, nếu không `updatePaintNode()` không bao giờ được gọi:

```cpp
setFlag( QQuickItem::HasContents, true );
```

## 3. Thay đổi trong QML

Trước:

```qml
FrameView {
    Layout.fillWidth: true
    Layout.fillHeight: true
    player: videoPlayer
    Label { anchors.centerIn: parent; ... }
}
```

Sau:

```qml
Item {
    Layout.fillWidth: true
    Layout.fillHeight: true
    Rectangle { anchors.fill: parent; color: "black" }
    FrameView {
        id: frameView
        anchors.fill: parent
        player: videoPlayer
        Label { anchors.centerIn: parent; ... }
    }
}
```

## 4. Kết quả mong đợi

| | Trước | Sau |
|---|---|---|
| CPU khi phát | 42–114% một lõi (chỉ vẽ) | **chỉ còn copy ảnh**, phần còn lại của GPU |

**Cần đo lại ở máy công ty** bằng `vmp_tools/cpu_test/cpu.cpp`.

Nếu máy dùng `llvmpipe`, việc vẽ vẫn tốn CPU (OpenGL giả lập). Đo được ở máy ảo: backend `software` nhẹ hơn (42–50% so với 76–114%).

```bash
QT_QUICK_BACKEND=software ./build/videoMetadataPlayer video.mp4
```

## 5. Những chỗ dễ hỏng

### 5.1 `updatePaintNode` không được gọi

**Hiện tượng:** màn hình đen, không có hình.

**Lý do:** thiếu `setFlag( QQuickItem::HasContents, true )`.

### 5.2 Hình nhảy hoặc nháy

**Hiện tượng:** màn hình đen xen kẽ hình.

**Lý do thường gặp:** `setTexture( nullptr )` rồi `setRect( QRectF() )` khi ảnh null — nhưng nếu code tạo texture **mới** mỗi frame mà không giữ texture cũ, GPU phải xoá texture cũ giữa chừng → nháy.

**Cách xử lý:** giữ `m_image`, chỉ tạo texture khi `cacheKey()` đổi. Textures cũ được `setOwnsTexture(true)` giải phóng khi node thay thế.

### 5.3 `window()` trả về nullptr

**Hiện tượng:** không hiện gì.

**Lý do:** item chưa được thêm vào cửa sổ nào.

**Cách xử lý:** code đã kiểm tra `window() == nullptr` và trả về node rỗng, không crash.

### 5.4 Ảnh đọc ra bị đổi trong lúc GPU dùng

**Hiện tượng:** hình nhấp nháy, hoặc hiện hình cũ.

**Lý do:** nếu `createTextureFromImage` không copy ảnh mà giữ tham chiếu, và ảnh bị giải phóng, GPU đọc vùng nhớ đã free.

**Cách xử lý:** `createTextureFromImage` của Qt **có copy** (trừ khi dùng `createTextureFromImage` với `QSGTexture::ownsMemory`... mặc định là copy). Nhưng để chắc, `m_image = image` giữ một bản riêng trong item → ảnh còn sống suốt đời node.

**Ghi chú:** đây là điểm cần kiểm tra khi test. Nếu thấy nhấp nháy, dùng `image.copy()`.

### 5.5 Backend `software` không hỗ trợ texture như mong đợi

**Hiện tượng:** CPU không giảm khi dùng `QT_QUICK_BACKEND=software`.

**Lý do:** backend software của Qt Quick vẫn dùng `QSGSimpleTextureNode`, nhưng việc thu nhỏ vẫn nằm trên CPU. → **Cách sửa này không giải quyết vấn đề trên máy ảo.**

Đây là giới hạn của môi trường, không phải lỗi code. Trên máy thật có GPU thì thu nhỏ nằm trên GPU.

### 5.6 `updatePaintNode` chạy ở render thread, không phải GUI thread

**Hiện tượng:** đọc `m_image` (member của item) ở GUI thread (`setPlayer` gọi `update()`) và ở render thread → nghi vấn data race.

**Phân tích:** `m_image` chỉ được **ghi** ở render thread (`updatePaintNode`) và **đọc** ở render thread. GUI thread không chạm `m_image`. → **Không có race.**

`setPlayer()` chỉ gọi `connect`/`disconnect` và `update()` — không đụng `m_image`.

## 6. Việc có thể làm tiếp

Hồ sơ cũ mục 10.2 có nêu:

- **Bỏ `videoconvert` sang RGBA**, nhận thẳng I420, rồi đổi màu trên GPU bằng shader (`QSGMaterial` với 3 texture Y/U/V).
- Tiết kiệm thêm khoảng 10–20% một lõi, nhưng phức tạp hơn nhiều.
- **Lưu ý:** đọc mã vạch hiện dựa vào ảnh RGBA. Nếu đổi sang I420 thì đọc trực tiếp từ **mặt phẳng Y** — nhanh hơn nữa, vì độ sáng có sẵn.

Chưa làm vì cần biết trước có còn thời gian hay không.
