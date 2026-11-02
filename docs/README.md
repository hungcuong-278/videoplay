# Video Metadata Player — Hồ sơ bàn giao lần 2 (Mục 10 và Mục 11)

> Tài liệu này gom **toàn bộ** những gì đã làm sau hồ sơ bàn giao đầu tiên: mục lục keyframe, vẽ hình bằng texture, thời lượng ổn định, overlay thông số, cắt video không encode, đồ thị pan/tilt — kèm **toàn bộ lỗi có thể xảy ra** đã được nghĩ trước và cách xử lý.
>
> Mọi thứ nằm trong Git, đã push lên <https://github.com/hungcuong-278/videoplay>. Code chưa từng được build trên máy này (không có Qt/GStreamer), nên mọi thứ phải kiểm chứng ở máy công ty.
>
> **Mục 0 là phần tóm tắt dành cho trợ lý lập trình.** Đọc mục 0 trước, rồi mở đúng file `.md` trong `docs/` theo mục cần làm.

---

## Mục lục

| File | Nội dung | Khi nào đọc |
|---|---|---|
| `docs/00_tong_quan.md` | Đọc nhanh, trạng thái, việc cần làm | Luôn, trước tiên |
| `docs/01_quy_uoc_va_kien_truc.md` | Quy ước team, kiến trúc đa luồng, các quyết định và lý do | Khi cần sửa code |
| `docs/02_muc_luc_keyframe.md` | Mục 10.1 — sửa CPU 400% khi tua | Khi làm tiếp 10.1 |
| `docs/03_ve_bang_texture.md` | Mục 10.2 — sửa khung hình giật | Khi làm tiếp 10.2 |
| `docs/04_thoi_luong_on_dinh.md` | Mục 10.3 — sửa thời lượng nhảy | Khi làm tiếp 10.3 |
| `docs/05_overlay_thong_so.md` | Mục 11.3 — overlay kỹ thuật | Khi làm tiếp 11.3 |
| `docs/06_cat_video.md` | Mục 11.4 — cắt video không encode | Khi làm tiếp 11.4 |
| `docs/07_do_thi_pan_tilt.md` | Mục 11.5 — đồ thị pan/tilt | Khi làm tiếp 11.5 |
| `docs/08_loi_tien_mung.md` | **Toàn bộ lỗi có thể xảy ra** và cách xử lý | **Trước khi test** |
| `docs/09_keichung_chi_tiet.md` | Số đo, kết quả mong đợi, quy trình test | Khi test |
| `docs/10_cong_cu_do.md` | Các công cụ đo lường và cách dùng | Khi đo |

---

## 0. Đọc nhanh

### Trạng thái

**Đã xong code, chưa build, chưa test.** Tất cả những gì dưới đây là code đã viết theo đúng chuẩn team và đã qua kiểm tra tĩnh, nhưng **chưa từng chạy một lần**. Máy đồ dùng để viết là Windows, không có Qt và GStreamer.

Nếu build lỗi, đó là lỗi thiếu include hoặc sai chữ ký — hãy đọc `docs/08_loi_tien_mung.md` mục "Lỗi build thường gặp" trước khi sửa.

### Đã làm được (theo mục của hồ sơ cũ)

| Mục | Trạng thái | File chính |
|---|---|---|
| 10.1 Mục lục keyframe | ✅ Code xong, chưa test | `src/index/keyframeIndex.hpp/.cpp`, `src/index/nalParser.hpp/.cpp` |
| 10.1 Nối vào gate | ✅ Code xong, chưa test | `src/player/videoPlayer.hpp/.cpp` |
| 10.2 Vẽ bằng texture | ✅ Code xong, chưa test | `src/player/frameView.hpp/.cpp` |
| 10.3 Thời lượng ổn định | ✅ Code xong, chưa test | `src/player/videoPlayer.cpp` |
| 11.3 Overlay thông số | ✅ Code xong, chưa test | `qml/main.qml`, `src/player/videoPlayer.cpp` |
| 11.4 Cắt video | ⚠️ Code xong, **chỉ hỗ trợ MPEG-TS** | `src/cutter/videoCutter.hpp/.cpp`, `src/cutter/cutController.hpp/.cpp` |
| 11.5 Đồ thị pan/tilt | ✅ Code xong, chưa test | `src/views/panTiltGraph.hpp/.cpp` |

### Việc chưa làm

- **Chưa có file MP4 trong `copyStream`** (mục 11.4). Xem `docs/06_cat_video.md`.
- **Chưa hỗ trợ bấm vào đồ thị pan/tilt để tua** (ý tưởng của hồ sơ cũ, mục 11.5). Đồ thị hiện chỉ hiển thị.
- **Chưa chạy ThreadSanitizer** trên code mới.
- **Chưa đo lại hiệu năng** — không có con số thật nào trong tài liệu này, tất cả là **kỳ vọng** đã ghi rõ.

### Bốn điều đáng chú ý nhất

1. **So sánh PTS thô, không so sánh stream time.** File TS bắt đầu ở PTS 3600 s nhưng hiển thị từ 125 ms. Nếu gate so sánh bằng stream time thì nó sẽ chờ một keyframe đã đi qua qua, và seek sẽ không bao giờ hạ cánh. Đây là lý do `KeyframeIndex` lưu **hai** mốc thời gian cho mỗi keyframe.

2. **`WAIT_UNTIL_ENTRY` bỏ hình theo PTS thô, nên nó có thể bỏ sót hình đích.** Nếu hình đích không hiện, tăng dung sai từ 2 ms lên 20 ms — hoặc tốt hơn, so sánh bằng stream time kèm mốc offset lấy từ mục lục. Chi tiết ở `docs/08_loi_tien_mung.md`.

3. **Cắt video dùng `gst_element_query_position` để biết đã tới cuối đoạn.** Với TS, vị trí này có thể lệch thời gian thật, nên file cắt có thể bị cắt sớm hoặc muộn. Nếu anh thấy vậy, đổi sang đếm hình qua nhánh appsink.

4. **Overlay không hiện codec/size/fps ngay khi mở file.** Nó chỉ có dữ liệu sau `ASYNC_DONE` đầu tiên. Bấm play một lần thì hiện.

---

## Cấu trúc repository

```
videoplay/                        <- clone từ GitHub
├── .gitignore                    <- .mp4/.csv/.ts bị ignore
├── videoMetadataPlayer.pro       <- file qmake của sản phẩm
├── qml.qrc
├── README.md
├── src/                          <- TOÀN BỘ CODE SẢN PHẨM
│   ├── main.cpp                  <- 96 dòng
│   ├── player/
│   │   ├── videoPlayer.hpp/.cpp  <- 382 / 1238
│   │   └── frameView.hpp/.cpp    <- 68 / 99    (Mục 10.2, viết lại)
│   ├── index/
│   │   ├── nalParser.hpp/.cpp    <- 82 / 173   (MỚI)
│   │   └── keyframeIndex.hpp/.cpp<- 260 / 763  (MỚI)
│   ├── metadata/
│   │   ├── barcodeReader.hpp/.cpp<- 70 / 277   (giữ nguyên từ hồ sơ cũ)
│   │   ├── metadataTable.hpp     <- 33
│   │   └── metadataStore.hpp/.cpp<- 163 / 417
│   ├── cutter/
│   │   ├── videoCutter.hpp/.cpp  <- 222 / 579  (MỚI)
│   │   └── cutController.hpp/.cpp<- 116 / 265  (MỚI)
│   └── views/
│       └── panTiltGraph.hpp/.cpp <- 109 / 169   (MỚI)
├── qml/main.qml                  <- 458 dòng
├── docs/                         <- TÀI LIỆU BÀN GIAO (12 file .md)
│   ├── README.md                 <- mục lục, đọc file nào trước
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
│   └── handoff_10_11.md          <- bản rút gọn của bản gốc
└── vmp_tools/                    <- công cụ đo, KHÔNG thuộc sản phẩm
    ├── keyframe_test/             <- keyframeIndexTool, countIndexed (MỚI)
    ├── decode_count/count.cpp    <- đếm buffer vào decoder
    ├── seek_lat/lat.cpp          <- đo độ trễ tua
    ├── seek_test/step.cpp        <- đo tua từng frame
    ├── drag_test/drag.cpp        <- mô phỏng kéo thanh trượt
    ├── seek_tsan/chaos.cpp       <- gây race để TSan bắt
    ├── cpu_test/cpu.cpp          <- đo CPU khi phát
    ├── bc_test/bcTest.cpp        <- kiểm tra bộ đọc mã vạch
    ├── tool/tool.pro             <- .pro dùng chung cho 5 tool trên
    └── tools/c128_all.py         <- sinh ảnh mẫu cho bộ đọc mã vạch
```

Tổng sản phẩm: **4 838 dòng** (không tính `vmp_tools`).

### Vì sao chia thành 5 module

| Module | Chứa năng | Phụ thuộc |
|---|---|---|
| `player` | Phát video, seek, gate giải mã, hiển thị hình | `index`, `metadata` |
| `index` | Mục lục keyframe, đọc NAL | — |
| `metadata` | CSV, mã vạch | — |
| `cutter` | Cắt video, điều phối đánh dấu | `player`, `metadata` |
| `views` | Đồ thị pan/tilt | — |

**Quy tắc:** `index`, `metadata`, `views` **không** được biết `player` hay `cutter`.

→ Cho phép thêm một công cụ đo chỉ cần `index` mà không phải link cả `player`.

---

## Lịch sử commit

| Commit | Nội dung |
|---|---|
| `a650c7b` | Import trạng thái project từ máy công ty (commit 1–5) |
| `6852c33` | Thêm mục lục keyframe cho container không có index |
| `ff6d808` | Dùng mục lục cho seek chính xác |
| `c64d589` | Vẽ hình bằng texture thay vì QPainter |
| `f07a419` | Overlay thông số kỹ thuật trên hình |
| `ab6ee85` | Cắt video không làm thay đổi hình |
| `5e9bd49` | Đồ thị pan/tilt |
| `0a4dacd` | Giữ thời lượng ổn định trên file không có index |
| `f0a2f4b` | Mô tả code mới để test ở máy công ty |
| `ee95595` | Thêm công cụ đo vào repository |

Còn tồn tại một thư mục Git cũ ở `d:\Viettel\video\.git` (repo rỗng, không có commit nào). Nó **không liên quan** và đã bị xoá khi dọn dẹp.

---

## Cách làm việc với tài liệu này

- Muốn biết **code đã viết thế nào**: đọc file `.md` trong `docs/` tương ứng.
- Muốn biết **có thể hỏng ở đâu**: đọc `docs/08_loi_tien_mung.md` — đó là phần quan trọng nhất.
- Muốn biết **làm sao để chứng minh là xong**: đọc `docs/09_keichung_chi_tiet.md`.
- Muốn **sửa code**: đọc `docs/01_quy_uoc_va_kien_truc.md` trước để không phá chuẩn team.

Mỗi file trong `docs/` đều tự đủ thông tin, không cần đọc theo thứ tự — trừ `00_tong_quan.md`.
