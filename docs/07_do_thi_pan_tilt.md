# 07 — Mục 11.5: Đồ thị pan/tilt

Giải quyết: **hiển thị quỹ đạo pan/tilt của camera theo video**.

File: `src/views/panTiltGraph.hpp` (109 dòng), `src/views/panTiltGraph.cpp` (169 dòng), `src/metadata/metadataStore.cpp` (`panTiltData`, `panTiltValues`), `qml/main.qml`.

---

## 1. Vấn đề gốc

CSV có 65 cột, trong đó chỉ 3 cột thay đổi: `pan`, `tilt`, `observationMode`. 61 cột còn lại là hằng số.

Các cột tọa độ đều bằng 0 hoặc `-214.748365` (giá trị không hợp lệ) → **không vẽ bản đồ được**.

→ Vì vậy phần làm thêm chọn **biểu đồ pan/tilt chạy theo video**.

## 2. Giao diện

Một `PanTiltGraph` (`QQuickPaintedItem`) ở dưới thanh trượt.

```
  pan
  45 ┤ ╱╲    ╱╲
  30 ┤╱  ╲╱╲╱  ╲
  15 ┤              ╲╱
   0 ┼────────────────────────────
     0    30    60    90   120 s
```

- **Đường xanh** = pan
- **Đường đỏ** = tilt
- **Đường dọc + chấm trắng** = vị trí hiện tại

## 3. Trục X là **chỉ số dòng CSV**, không phải thời gian

**Đây là điểm quan trọng nhất, và là điểm khác biệt với cách em đã mô tả trong hồ sơ cũ.**

### 3.1 Vì sao

`PanTiltGraph` nhận:

```cpp
Q_INVOKABLE void setValues( const QVector<double> &_pan,
                            const QVector<double> &_tilt );
```

Hai danh sách **cùng độ dài**, theo **thứ tự dòng của bảng CSV**. Không có mốc thời gian nào trong đó.

**Lý do thiết kế:** CSV **không có cột thời gian**. Chỉ có `imageId`. Nếu muốn trục X là thời gian thì phải biết thời gian của từng hình — mà `VideoPlayer` chỉ biết thời gian của những hình **đã xem**, và thứ tự đã xem bị phá khi người dùng tua.

→ Chọn trục X = **chỉ số dòng** là lựa chọn **đúng và chắc**: nó luôn khớp 1-1 với dòng CSV, và người dùng đối chiếu được trực tiếp với file CSV.

### 3.2 Hệ quả: chấm trắng dùng `row.row`, không dùng thời gian

```qml
function updatePosition() {
    if (row.found && panValues.length > 0)
        setPosition(row.row)
    else
        setPosition(-1)
}
```

`row.row` là **chỉ số dòng** trong bảng CSV (0 = dòng dữ liệu đầu tiên). Nhờ vậy chấm trắng **luôn** nằm đúng chỗ của hình đang hiện, kể cả sau khi tua.

**Đây là ưu điểm lớn so với cách dùng trục thời gian:** tua tới đâu cũng đúng, không bao giờ lệch.

### 3.3 Đánh đổi

- Không biết trục X là bao nhiêu giây → **không có nhãn thời gian** trên đồ thị.
- Muốn biết "hình này ở giây nào" thì phải tra bảng.

→ Nếu anh muốn thêm trục thời gian, cần mở rộng `KeyframeIndex` để lưu PTS của **mọi** hình (hiện chỉ lưu keyframe). Đó là việc đáng kể.

**Cách rẻ hơn:** vẽ trục X theo **ID** thay vì chỉ số dòng. Vì ID của dòng `i` là `firstId + i` (trong file mẫu ID liên tục 1→3513), nên trục X có thể ghi nhãn ID mà không cần thêm dữ liệu nào.

## 4. `MetadataStore::panTiltData` — chuẩn bị dữ liệu

### 4.1 API

```cpp
Q_INVOKABLE QVariantList panTiltData() const;
void panTiltValues( QVector<double> &pan_, QVector<double> &tilt_ ) const;
```

`panTiltData()` trả về một `QVariantList` gồm **hai phần tử**:

| Vị trí | Ý nghĩa |
|---|---|
| `[0]` | `QVariantList` của các giá trị pan |
| `[1]` | `QVariantList` của các giá trị tilt |

**Giá trị `NaN`** khi dòng đó không có số trong cột đó. Đồ thị **để lỗ hổng** ở chỗ đó.

### 4.2 Chạy ở đâu

`panTiltValues()` là hàm `const`, đọc `m_table` (bất biến, dùng chung được) → **có thể gọi ở bất kỳ thread nào**.

→ QML gọi nó trực tiếp. Không cần thread riêng, không cần callback.

**Đây là khác biệt so với bản mô tả trong hồ sơ cũ** (hồ sơ cũ nói dựng dữ liệu đồ thị trên pool thread cùng lúc parse CSV). Code thật làm đơn giản hơn và **đúng hơn**, vì bảng sau khi tải xong là bất biến.

**Đánh giá chi phí:** 3 513 dòng, mỗi dòng split và parse 2 số → khoảng 1–3 ms. Gọi lại mỗi khi bảng đổi là hoàn toàn chấp nhận được.

### 4.3 Cách đọc giá trị

```cpp
for ( int i = 0; i < count; ++i )
{
    QList<QByteArray> fields = splitLine( m_table->lines.at( i ) );
    double pan = qQNaN();
    double tilt = qQNaN();
    if ( m_table->panColumn >= 0 && m_table->panColumn < fields.size() )
    {
        pan = fields.at( m_table->panColumn ).toDouble();
    }
    ...
}
```

`m_table->panColumn` / `tiltColumn` được tìm **một lần lúc parse** (đọc tên cột từ header). Nếu không tìm thấy cột → `-1` → mọi giá trị là NaN → đồ thị trống.

**Lý do dùng `qQNaN()` chứ không dùng 0:** 0 là một giá trị pan/tilt **hợp lệ**. Nếu điền 0 cho dòng thiếu dữ liệu, đồ thị sẽ vẽ một đường thẳng về 0 — sai. NaN cho phép vẽ lỗ hổng.

## 5. `PanTiltGraph` — vẽ bằng `QPainter`

### 5.1 Vì sao `QQuickPaintedItem` mà không phải texture

Đồ thị là **đường vector**, và chỉ đổi khi CSV đổi (không phải mỗi frame). Vẽ bằng `QPainter` là **hoàn toàn đủ** và đơn giản hơn nhiều.

Ngược lại với hình video: 1280×720×4 byte × 25 frame/s → phải đưa lên GPU.

**Quy tắc:** dùng công cụ đơn giản nhất khi dữ liệu nhỏ; dùng texture khi dữ liệu lớn và đổi liên tục.

### 5.2 Ba property

```cpp
Q_PROPERTY( QColor panColor READ panColor WRITE setPanColor
            NOTIFY colorsChanged )
Q_PROPERTY( QColor tiltColor READ tiltColor WRITE setTiltColor
            NOTIFY colorsChanged )
```

Màu đổi được từ QML → không hard-code.

### 5.3 `setValues`

```cpp
Q_INVOKABLE void setValues( const QVector<double> &_pan,
                            const QVector<double> &_tilt )
{
    if ( m_pan == _pan && m_tilt == _tilt )
    {
        return;
    }
    m_pan = _pan;
    m_tilt = _tilt;
    update();
}
```

So sánh **trước** khi gán → không vẽ lại vô ích khi QML gọi lại với cùng dữ liệu.

**Điểm quan trọng:** QML gọi `setValues` **hai lần** khi bảng đổi (`onPanValuesChanged` và `onTiltValuesChanged`). Kiểm tra này loại bỏ lần vẽ thừa.

### 5.4 `setPosition`

```cpp
Q_INVOKABLE void setPosition( int _position )
{
    if ( m_position == _position )
    {
        return;
    }
    m_position = _position;
    update();
}
```

`_position` là **chỉ số dòng** (0-based), hoặc `-1` = không có.

### 5.5 `valueRange`

```cpp
bool PanTiltGraph::valueRange( double &low_, double &high_ ) const
{
    low_ = 0.0;
    high_ = 1.0;
    bool found = false;
    for ( const double value : m_pan )   // và m_tilt
    {
        if ( qIsNaN( value ) )
        {
            continue;
        }
        if ( !found ) { low_ = value; high_ = value; found = true; }
        else { low_ = qMin( low_, value ); high_ = qMax( high_, value ); }
    }
    return found;
}
```

**Vì sao bỏ qua NaN:** NaN là "không có dữ liệu", không phải giá trị. Nếu không bỏ, `qMin` với NaN cho kết quả NaN → toàn bộ đồ thị vẽ sai.

**Vì sao khởi đầu 0.0 / 1.0:** để đồ thị không bị dẹp khi dữ liệu gần như hẳng số.

**Vì sao dùng `found` riêng:** nếu chỉ dùng `low_`/`high_` khởi tạo 0/1, thì dữ liệu toàn số dương sẽ bị kéo xuống 0 → đồ thị dùng nửa trên không.

### 5.6 `paintLine`

```cpp
void PanTiltGraph::paintLine( QPainter *_painter,
                              const QVector<double> &_values,
                              double _low, double _high,
                              const QColor &_color ) const
{
    QPainterPath path;
    bool penDown = false;
    for ( int i = 0; i < _values.size(); ++i )
    {
        const double value = _values.at( i );
        if ( qIsNaN( value ) )
        {
            penDown = false;      // lift the pen, leave a gap
            continue;
        }
        const qreal x = mapX( i, _values.size() );
        const qreal y = mapY( value, _low, _high );
        if ( penDown )
        {
            path.lineTo( x, y );
        }
        else
        {
            path.moveTo( x, y );
            penDown = true;
        }
    }
    _painter->setPen( QPen( _color, 2.0 ) );
    _painter->drawPath( path );
}
```

**Vì sao `penDown`:** để vẽ **lỗ hổng** ở chỗ NaN, thay vì nối thẳng qua → tránh vẽ một đoạn thẳng chui qua vùng không có dữ liệu (gây hiểu nhầm).

### 5.7 `paint`

```cpp
void PanTiltGraph::paint( QPainter *_painter )
{
    double low = 0.0;
    double high = 1.0;
    if ( !valueRange( low, high ) )
    {
        return;         // no data at all
    }
    // margins, grid, axis labels
    // pan line
    paintLine( _painter, m_pan, low, high, m_panColor );
    // tilt line
    paintLine( _painter, m_tilt, low, high, m_tiltColor );
    // current position
    if ( m_position >= 0 && m_position < m_pan.size() )
    {
        // vertical line + dot
    }
}
```

### 5.8 Vị trí hiện tại

Chấm trắng được vẽ ở **giá trị pan** của dòng `m_position`, và đường dọc ở `mapX( m_position, ... )`.

**Điểm cần biết:** nếu dòng `m_position` có pan là NaN thì không có chấm, nhưng đường dọc vẫn vẽ.

## 6. Luồng dữ liệu trong QML

```qml
PanTiltGraph {
    id: panTilt
    visible: metadata.loaded

    property var panValues: []
    property var tiltValues: []

    function graphValuesChanged() {
        var data = metadata.panTiltData
        if (data && data.length >= 2) {
            panValues = data[0]
            tiltValues = data[1]
            setValues(panValues, tiltValues)
        }
    }

    onPanValuesChanged: setValues(panValues, tiltValues)
    onTiltValuesChanged: setValues(panValues, tiltValues)

    function updatePosition() {
        if (row.found && panValues.length > 0)
            setPosition(row.row)
        else
            setPosition(-1)
    }

    Connections {
        target: metadata
        onPanTiltChanged: panTilt.graphValuesChanged()
    }

    Connections {
        target: videoPlayer
        onFrameReady: panTilt.updatePosition()
    }
}
```

### 6.1 Vì sao `graphValuesChanged()` được gọi từ `onPanTiltChanged`

`MetadataStore` phát `panTiltChanged()` khi bảng mới tải xong → QML lấy dữ liệu mới.

`onVisibleChanged` và `onWidthChanged` cũng gọi lại — để xử lý trường hợp đồ thị hiện ra sau khi dữ liệu đã có, và vẽ lại khi đổi kích thước.

### 6.2 `row.row` là gì

`MetadataStore::rowInfo( frameId )` trả về map:

| Khóa | Ý nghĩa |
|---|---|
| `found` | Tìm thấy dòng hay không |
| `row` | **Chỉ số dòng** (0-based) trong bảng |
| `pan` / `tilt` / `mode` | Giá trị của dòng |
| `values` | Toàn bộ giá trị của dòng |

`row.row` là chỉ số dòng → dùng làm trục X cho đồ thị.

**Lý do `row.found` phải được kiểm tra:** hình không có trong CSV (đặc biệt file 2 giờ có ID lặp) → không có dòng → không có chấm trắng.

## 7. Những chỗ dễ hỏng

### 7.1 Đồ thị trống

**Hiện tượng:** khung đồ thị không vẽ gì.

**Lý do thường gặp:**

1. CSV chưa tải xong → `metadata.loaded` false → `visible: false`.
2. Không tìm thấy cột `pan`/`tilt` trong header → `panColumn = -1` → mọi giá trị NaN → `valueRange` trả false → `paint` return sớm.
3. Bảng rỗng.

**Cách chẩn đoán:** kiểm tra `metadata.loaded`, `metadata.rowCount`, `metadata.panTiltData[0].length` trong QML.

### 7.2 Đồ thị lệch với hình đang hiện

**Hiện tượng:** chấm trắng không nằm ở hình tương ứng.

**Lý do:** `row.row` là chỉ số dòng của CSV, còn đồ thị vẽ theo chỉ số dòng → **luôn khớp**.

→ Vấn đề này **không thể xảy ra** với thiết kế hiện tại. Nếu thấy lệch, kiểm tra `row.found`.

**Đây là ưu điểm lớn của việc chọn trục X = chỉ số dòng.**

### 7.3 Chấm trắng nhảy lung tung khi tua

**Hiện tượng:** chấm nhảy.

**Lý do:** ID trong video **không đơn điệu** (file mẫu: frame thứ 3459 mang ID 3460; file 2 giờ: ID lặp). Nên `row.row` không tăng đều.

**Không phải lỗi** — đó là dữ liệu thật.

### 7.4 Đồ thị vẽ ra ngoài khung

**Hiện tượng:** đường vẽ đè lên nhãn.

**Lý do:** `valueRange` tính từ dữ liệu, nhưng vị trí hiện tại có thể nằm ngoài khoảng đó.

**Cách xử lý:** clamp giá trị hiện tại khi vẽ.

### 7.5 Đồ thị vẽ lại liên tục khi phát

**Hiện tượng:** CPU tăng nhẹ khi phát.

**Lý do:** `setPosition` đổi mỗi frame → `update()` → vẽ lại **toàn bộ** đồ thị (3 513 điểm × 2 đường).

**Mức độ:** 7 000 điểm mỗi frame × 25 fps = 175 000 điểm/s. Không nặng lắm, nhưng đáng để biết.

**Cách xử lý nếu thấy nặng:** cache hai đường vào `QPixmap` một lần, mỗi frame chỉ vẽ chấm + đường dọc lên pixmap.

### 7.6 File 2 giờ có ID lặp → đồ thị chỉ hiển thị 1 chu kỳ

**Hiện tượng:** đường đi lại về đầu.

**Lý do:** bảng CSV chỉ có 3 513 dòng, ID 1→3513. File 2 giờ lặp ID 1→3462. `rowById` index theo ID → ID trùng ghi đè.

**Chấp nhận được** — file 2 giờ chỉ dùng để đo hiệu năng.

### 7.7 `pan`/`tilt` không phải số → cột đồ thị trống

**Lý do:** `toDouble()` trả 0.0 khi không parse được (không phải NaN!).

**Điểm cần chú ý:** `QByteArray::toDouble()` trả **0.0** khi thất bại, không trả NaN. Nếu muốn NaN cho dòng lỗi thì phải kiểm tra `ok` bằng `toDouble(&ok)`.

**Kiểm chứng:** xem lại `panTiltValues()` — nếu nó gọi `toDouble()` không có `ok` thì dòng lỗi sẽ hiện giá trị 0 thay vì lỗ hổng.

### 7.8 `setValues` được gọi với danh sách rỗng

**Hiện tượng:** đồ thị trống sau khi đổi CSV.

**Lý do:** `metadata.panTiltData` trả về list rỗng nếu bảng null.

**Cách xử lý:** QML đã kiểm tra `data.length >= 2` ✅.

## 8. Việc có thể làm tiếp

| Việc | Ghi chú | Độ khó |
|---|---|---|
| **Thêm trục X theo ID** | ID của dòng `i` = `firstId + i` → chỉ cần đổi nhãn, không cần thêm dữ liệu | Thấp |
| **Bấm vào đồ thị để tua** | Cần tra ngược chỉ số dòng → ID → PTS hình. PTS của mọi hình chưa có | Cao |
| Cache đồ thị vào pixmap | Nếu thấy nặng | Thấp |
| Tách pan và tilt thành 2 khung | Dễ đọc hơn khi hai giá trị khác thang | Thấp |
| Thêm trục thời gian thật | Cần mở rộng `KeyframeIndex` lưu PTS mọi hình | Cao |
| Hiện `observationMode` | Cột thứ 3 thay đổi, chưa vẽ | Thấp |

## 9. Câu hỏi cần hỏi anh Trí

| Câu hỏi | Vì sao |
|---|---|
| Trục X nên là **ID** thay vì chỉ số dòng? | ID khớp trực tiếp với CSV, dễ đối chiếu |
| Cần bấm vào đồ thị để tua không? | Hồ sơ cũ có nêu, hiện chưa làm, và làm sẽ tốn công |
| Có cần hiện cả `observationMode` không? | Cột thứ 3 thay đổi, chưa vẽ |
