# Video Metadata Player

Trình phát video có đọc mã vạch (Code 128) trên từng hình, khớp với bảng
metadata CSV, và cắt ra video mới + CSV mới **khớp 100%** mà không encode lại.

## Công nghệ

| | |
|---|---|
| Ngôn ngữ | C++11 |
| Giao diện | Qt 5.12.7 + qmake + QML |
| Video | GStreamer 1.16 |
| Hệ điều hành | Linux (đã kiểm chứng trên Ubuntu 20.04) |

Chỉ dùng API có từ **Qt 5.12** và **GStreamer 1.16**.

## Cách build

```bash
git clone https://github.com/hungcuong-278/videoplay.git
cd videoplay
mkdir -p build && cd build
qmake ../videoMetadataPlayer.pro
make -j"$(nproc)"
```

Trên máy công ty, dùng Qt 5.12.7 của installer:

```bash
/home/trungnd233/Qt5.12.7/5.12.7/gcc_64/bin/qmake ../videoMetadataPlayer.pro
```

## Cách chạy

```bash
./videoMetadataPlayer [video] [csv]
```

- Không có tham số → mở giao diện, chọn file trong app.
- Chỉ có video → CSV cùng tên cạnh video được nạp tự động.
- Có cả hai → dùng đúng CSV đó.

## Phím tắt

| Phím | Việc |
|---|---|
| `Space` | Phát / dừng |
| `←` / `→` | Lùi / tiến một hình |
| `Ctrl + ←` / `Ctrl + →` | Tua ±5 giây |
| `O` | Bật / tắt overlay thông số |
| `I` | Mark In (đầu đoạn cắt) |
| `U` | Mark Out (cuối đoạn cắt) |

## Cấu trúc

```
├── src/
│   ├── player/     phát video, seek, gate giải mã, hiển thị hình
│   ├── index/      mục lục keyframe, đọc NAL
│   ├── metadata/   CSV, bộ đọc mã vạch
│   ├── cutter/     cắt video, điều phối đánh dấu
│   └── views/      đồ thị pan / tilt
├── qml/            giao diện
├── docs/           tài liệu bàn giao chi tiết
└── vmp_tools/      công cụ đo lường (không thuộc sản phẩm)
```

## Tài liệu bàn giao

Toàn bộ chi tiết kỹ thuật nằm trong [`docs/`](docs/README.md):

| File | Nội dung |
|---|---|
| [`00_tong_quan.md`](docs/00_tong_quan.md) | Đọc nhanh, trạng thái, việc cần làm |
| [`01_quy_uoc_va_kien_truc.md`](docs/01_quy_uoc_va_kien_truc.md) | Quy ước team, kiến trúc đa luồng |
| [`02_muc_luc_keyframe.md`](docs/02_muc_luc_keyframe.md) | Mục lục keyframe (mục 10.1) |
| [`03_ve_bang_texture.md`](docs/03_ve_bang_texture.md) | Vẽ hình bằng texture (mục 10.2) |
| [`04_thoi_luong_on_dinh.md`](docs/04_thoi_luong_on_dinh.md) | Thời lượng ổn định (mục 10.3) |
| [`05_overlay_thong_so.md`](docs/05_overlay_thong_so.md) | Overlay thông số (mục 11.3) |
| [`06_cat_video.md`](docs/06_cat_video.md) | Cắt video không encode (mục 11.4) |
| [`07_do_thi_pan_tilt.md`](docs/07_do_thi_pan_tilt.md) | Đồ thị pan/tilt (mục 11.5) |
| [`08_loi_tien_mung.md`](docs/08_loi_tien_mung.md) | **Sổ lỗi tiềm ẩn và rủi ro** |
| [`09_keichung_chi_tiet.md`](docs/09_keichung_chi_tiet.md) | Quy trình test, kết quả mong đợi |
| [`10_cong_cu_do.md`](docs/10_cong_cu_do.md) | Công cụ đo lường |

## Quy tắc viết code

Bắt buộc theo chuẩn team:

- Header đuôi `.hpp`, tên file = tên class chữ đầu thường.
- Mỗi `.cpp` chỉ include header của chính nó (trừ `main.cpp`).
- Tham số vào `_name`, ra `name_`, member `m_name`, hằng số `ALL_CAPS`.
- Dấu ngoặc Allman, `if ( a == 2 )`, thụt lề 4, **tối đa 80 ký tự mỗi dòng**.
- Doc comment kiểu Qt (`/*! \brief ... */`).
- Không dùng `using namespace`.
- **Comment trong code bằng tiếng Anh.**
