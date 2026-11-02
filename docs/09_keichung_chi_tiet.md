# 09 — Kiểm chứng chi tiết: quy trình test và kết quả mong đợi

Tài liệu này là **kế hoạch test**. Mỗi bước có: cách chạy, kết quả mong đợi, và cách chẩn đoán nếu sai.

**Tất cả số liệu trong bảng "Kết quả mong đợi" là KỲ VỌNG, chưa đo được.** Khi test xong, ghi số thật vào cột "Đã đo".

---

## 0. Chuẩn bị

### 0.1 Biên dịch

```bash
cd ~/cuong_workspace/videoplay
mkdir -p build && cd build
/home/trungnd233/Qt5.12.7/5.12.7/gcc_64/bin/qmake ../videoMetadataPlayer.pro
make -j"$(nproc)"
```

**Thời gian:** 30–60 giây lần đầu, 10–15 giây các lần sau.

### 0.2 File test cần chuẩn bị

| File | Nguồn | Dùng để |
|---|---|---|
| `video.mp4` (thật ra là TS) | `~/Downloads/2026-07-29_15-42-30/2026-07-29_15-42-30.mp4` | test chính |
| `video.csv` | cùng thư mục | test metadata |
| `long60.ts` | tạo bằng ffmpeg (xem 0.3) | đo hiệu năng seek |
| `long120.ts` | tạo bằng ffmpeg | test file 2 tiếng |

### 0.3 Tạo file test dài

```bash
cd ~/Downloads/2026-07-29_15-42-30
ffmpeg -v error -stats -y -stream_loop 25 -i 2026-07-29_15-42-30.mp4 \
  -c copy -f mpegts long60.ts
ffmpeg -v error -stats -y -stream_loop 51 -i 2026-07-29_15-42-30.mp4 \
  -c copy -f mpegts long120.ts
```

- `long60.ts`: khoảng 60 phút, 3,15 GB.
- `long120.ts`: khoảng 2:00:01, 6,3 GB.

**Kiểm tra:** `ls -lh long60.ts long120.ts`

### 0.4 Công cụ đo

```bash
cd vmp_tools/keyframe_test
qmake keyframe_test.pro && make -j4
qmake count_indexed.pro && make -j4
```

**Lưu ý:** `.pro` của công cụ dùng `VMP = ../../src` nên build được từ bất kỳ đâu, không phụ thuộc đường dẫn máy.

---

## 1. Build sạch

### 1.1 Không có lỗi, không có warning

```bash
cd build
make clean && make -j4 2>&1 | tee build.log
grep -icE "error" build.log     # phải là 0
grep -icE "warning" build.log   # nên là 0
```

**Nếu có lỗi:** xem `08_loi_tien_mung.md` mục A8.

### 1.2 Không có dòng quá 80 ký tự

```bash
cd ..
for f in $(git ls-files 'src/*.cpp' 'src/**/*.cpp' 'src/*.hpp' 'src/**/*.hpp'); do
  awk -v F="$f" 'length($0)>80 {print F": line "NR": "length($0)" chars"}' "$f"
done
```

Phải ra **rỗng**. Nếu có, sửa cho đúng chuẩn team.

**Lưu ý:** `git ls-files 'src/*.cpp'` với dấu `*` trong git **không** khớp dấu `/`. Dùng `git ls-files 'src/'` để lấy hết, hoặc dùng `find`.

### 1.3 Mỗi `.cpp` chỉ include header của chính nó

```bash
for f in $(git ls-files 'src/*.cpp' 'src/**/*.cpp'); do
  echo "$f -> $(grep -c '#include "' "$f") local include(s)"
done
```

Kết quả mong đợi: mọi file có `1`, trừ `src/main.cpp` có `0` hoặc nhiều hơn.

### 1.4 Không có file media trong git

```bash
git ls-files | grep -iE "\.(mp4|ts|mkv|avi|csv|vmpidx)$"
```

Phải ra **rỗng**.

---

## 2. Test mục lục keyframe (mục 10.1)

### 2.1 Số keyframe khớp ffprobe

```bash
# ffprobe
ffprobe -v error -select_streams v:0 \
  -show_entries frame=key_frame -of csv=p=0 video.mp4 | grep -c '^1$'

# bộ quét của em
./vmp_tools/keyframe_test/keyframeIndexTool video.mp4
```

| | Kết quả mong đợi | Đã đo |
|---|---|---|
| ffprobe | 28 | |
| keyframeIndexTool | 28 | |
| Lệch | 0 | |

**Nếu lệch:** xem `08_loi_tien_mung.md` mục D1 (recovery point).

### 2.2 Thời gian quét

```bash
time ./vmp_tools/keyframe_test/keyframeIndexTool long60.ts
```

| File | Kỳ vọng |
|---|---|
| `video.mp4` (138 s) | < 1 giây |
| `long60.ts` (3,15 GB) | 10–25 giây |
| `long120.ts` (6,3 GB) | 15–40 giây |

**Lưu ý:** chạy lần hai phải **nhanh hơn nhiều** (đọc cache `.vmpidx`).

### 2.3 Cache hoạt động

```bash
# lần 1: quét thật
time ./vmp_tools/keyframe_test/keyframeIndexTool long60.ts
ls -la long60.ts.vmpidx

# lần 2: đọc cache
time ./vmp_tools/keyframe_test/keyframeIndexTool long60.ts
```

**Kết quả mong đợi:** lần 2 nhanh hơn **ít nhất 10 lần**.

### 2.4 Cache bị vô hiệu khi file đổi

```bash
touch long60.ts
time ./vmp_tools/keyframe_test/keyframeIndexTool long60.ts
```

Phải quét lại (chậm).

### 2.5 Mục lục dùng được cho seek

```bash
./vmp_tools/keyframe_test/countIndexed long60.ts
```

Tool này chạy 8 seek chính xác, **hai lần**: tắt index rồi bật index.

| | Kỳ vọng | Đã đo |
|---|---|---|
| Tắt index | ~642 buffer/seek | |
| Bật index | ~117 buffer/seek | |
| Giảm | ~5,5 lần | |

**Đây là con số quan trọng nhất của toàn bộ mục 10.1.**

**Nếu không giảm:** xem `08_loi_tien_mung.md` mục H1.

---

## 3. Test vẽ bằng texture (mục 10.2)

### 3.1 Hình hiện

```bash
./build/videoMetadataPlayer video.mp4
```

**Phải thấy hình.** Nếu đen → `08_loi_tien_mung.md` mục A1.

### 3.2 Hình không nháy khi phát

Phát 2 phút, quan sát.

**Phải:** không nháy đen xen kẽ hình.

**Nếu nháy:** xem `08_loi_tien_mung.md` mục 5.2/5.4 trong `03_ve_bang_texture.md`.

### 3.3 CPU khi phát

```bash
./vmp_tools/cpu_test/cpu video.mp4
```

| | Trước | Sau | Đã đo |
|---|---|---|---|
| CPU khi phát | 42–114% một lõi | **?** | |

**Nếu không giảm:** kiểm tra `QSG_INFO=1` xem renderer là `llvmpipe` hay GPU thật.

```bash
QSG_INFO=1 ./build/videoMetadataPlayer video.mp4 2>&1 | grep -iE "renderer|vendor"
```

### 3.4 Hình vẫn đúng khi đổi kích thước cửa sổ

Kéo cửa sổ, resize.

**Phải:** hình co giãn giữ nguyên tỷ lệ, không méo, không vỡ.

---

## 4. Test thời lượng (mục 10.3)

### 4.1 Thời lượng không nhảy

```bash
./build/videoMetadataPlayer long120.ts
```

Đứng yên 60 giây, nhìn thanh trượt.

| | Trước | Sau | Đã đo |
|---|---|---|---|
| Thời lượng | 117:08 → 116:03 | **120:00.898** | |
| Nút thanh trượt | trượt | đứng yên | |

### 4.2 Thời lượng đúng sau khi mục lục xong

Mở file, đợi dòng "Index ready", so thời lượng hiện với:

```bash
ffprobe -v error -show_entries format=duration -of csv=p=0 long120.ts
```

Phải khớp trong vòng 100 ms.

### 4.3 Đóng file rồi mở file khác

Mở `long120.ts`, đóng, mở `video.mp4`.

**Phải:** thời lượng về đúng của file mới, không giữ giá trị cũ.

---

## 5. Test seek chính xác (phần quan trọng nhất)

### 5.1 Hạ cánh đúng frame

```bash
./vmp_tools/seek_lat/lat video.mp4
```

Tool chạy 8 seek chính xác tới các vị trí khác nhau, in ra độ trễ và **ID mã vạch** của hình hạ cánh.

**Phải:** ID đúng 8/8.

**Nếu ID sai:** xem `08_loi_tien_mung.md` mục A3 (dung sai 2 ms).

### 5.2 Độ trễ không tăng

| | Kỳ vọng | Đã đo |
|---|---|---|
| Trung bình | không tăng so với trước | |
| Lớn nhất | không tăng so với trước | |

### 5.3 Gate không bỏ quá nhiều hình

```bash
./vmp_tools/keyframe_test/countIndexed long60.ts
```

Số buffer phải **xấp xỉ** số frame từ keyframe tới mục tiêu, không phải nhỏ hơn nhiều.

**Nếu nhỏ hơn nhiều** (ví dụ 20 thay vì 117) → gate bỏ quá nhiều, hình đích không hiện.

### 5.4 Seek xem trước vẫn cho hình sạch

Kéo thanh trượt qua chỗ giữa video.

**Phải:** hình hiện ra **không xám, không méo**, mã vạch đọc được, ID đúng.

**Đây là hồ sơ cũ mục 8.7** — nếu xuất hiện thì lỗi 8.7 quay lại.

### 5.5 Tua bằng ← / →

Bấm ← → liên tục 20 lần.

**Phải:** không kẹt, không nhảy cóc, không crash.

### 5.6 Mở file nhanh liên tiếp

Mở A → B → C liên tiếp trong 2 giây.

**Phải:** không crash, không lẫn trạng thái giữa các file.

---

## 6. Test overlay (mục 11.3)

### 6.1 Bật/tắt bằng phím `O`

**Phải:** overlay hiện/ẩn.

### 6.2 Nội dung đầy đủ

Mở file, bấm play một lần, bấm `O`.

| Trường | Kỳ vọng |
|---|---|
| `ID` | 7 chữ số có số 0 ở đầu |
| `PTS` | khớp với thời gian trên thanh trượt |
| `PAN`/`TILT`/`MODE` | có số (nếu CSV đã nạp) |
| `SPEED` | 1x |
| `CODEC` | H.264 |
| `SIZE` | 1280 x 720 |
| `FPS` | 25.000 |
| `DEC BUF` | tăng dần khi phát |

### 6.3 Overlay cập nhật khi tua

Tua vài vị trí.

**Phải:** `ID` và `PTS` đổi theo.

### 6.4 Overlay không che mã vạch

Nhìn kỹ góc trên trái của hình.

**Nếu che:** di chuyển overlay sang góc trên phải.

---

## 7. Test cắt video (mục 11.4)

### 7.1 Mark In / Mark Out

1. Mở video, tua tới khoảng 100 s.
2. Bấm `I`.
3. Tua tới khoảng 103 s.
4. Bấm `U`.

**Phải:** thanh trượt vẽ đoạn đã chọn, hiện ID đầu và ID cuối.

### 7.2 Export

Bấm "Export", chọn nơi lưu.

**Kỳ vọng:** file `.ts` + file `.csv` được tạo, có thông báo kết quả.

### 7.3 Kiểm tra "khớp 100%" — cách kiểm chứng quan trọng nhất

```bash
# 1. Đếm frame của video mới
ffprobe -v error -select_streams v:0 \
  -count_frames -show_entries stream=nb_read_frames \
  -of csv=p=0 cut.ts

# 2. Đếm dòng của CSV mới (trừ header)
tail -n +2 cut.csv | wc -l

# 3. So
```

**Phải:** hai số **bằng nhau**.

### 7.4 Kiểm tra từng dòng khớp frame

```bash
# Lấy ID từ CSV mới
tail -n +2 cut.csv | cut -d, -f1 | head -20

# Đọc ID thật từ video mới bằng bcTest
./vmp_tools/bc_test/bcTest cut.ts | head -20
```

**Phải:** hai danh sách **giống nhau**, theo đúng thứ tự.

### 7.5 Đầu đoạn thực tế

So ID đầu tiên của CSV mới với ID mà người dùng đã đánh dấu.

**Kỳ vọng:** ID đầu **nhỏ hơn** (đoạn bắt đầu ở keyframe trước).

**Phải hiển thị cho người dùng** con số này.

### 7.6 Cắt đoạn ngắn

Cắt đoạn 1 giây.

**Phải:** không crash, báo lỗi rõ ràng nếu không làm được.

### 7.7 Cắt từ đầu file

 Tua tới 0, bấm `I`, rồi `U` ở 5 s.

**Phải:** file cắt bắt đầu đúng từ đầu.

### 7.8 Hủy giữa chừng

Bấm Export với đoạn dài, rồi bấm "Cancel".

**Phải:** dừng trong vài giây, file dang dở **được xoá**.

### 7.9 Đóng app khi đang cắt

Bấm Export, đóng app ngay.

**Phải:** không crash.

---

## 8. Test đồ thị pan/tilt (mục 11.5)

### 8.1 Đồ thị hiện

Nạp CSV, đợi vài giây.

**Phải:** hai đường pan (xanh) và tilt (đỏ).

### 8.2 Chấm trắng di chuyển

Phát video.

**Phải:** chấm trắng trượt theo.

### 8.3 Đồ thị không lệch

Phát từ đầu 30 giây, so vị trí chấm trắng với thời gian hiện tại.

### 8.4 Tải CSV mới

Nạp CSV khác.

**Phải:** đồ thị vẽ lại, không còn dữ liệu cũ.

---

## 9. Test đa luồng

### 9.1 ThreadSanitizer

```bash
cd vmp_tools/seek_tsan
cp ../tool/tool.pro .
# thêm vào tool.pro:
#   QMAKE_CXXFLAGS += -fsanitize=thread -g -O1
#   QMAKE_LFLAGS += -fsanitize=thread
qmake tool.pro && make -j4
TSAN_OPTIONS="halt_on_error=0 second_deadlock_stack=1" ./tool video.mp4 2> tsan.txt
grep -c "WARNING: ThreadSanitizer" tsan.txt
```

**Lu y:** `tool.pro` hien tro `VMP = ../../src`, nen chay duoc tu moi vi tri.

**Kết quả mong đợi:** các cảnh báo chỉ có stack `libQt5Core` / `QMutex` / futex (dương tính giả).

**Lỗi thật:** stack chứa **hai hàm của mình** cùng chạm một `m_*` không khoá.

### 9.2 Kịch bản gây race

```bash
./vmp_tools/seek_tsan/chaos video.mp4
```

Tool tua liên tục, đổi tốc độ, mở/đóng file.

### 9.3 Chạy 30 phút

```bash
timeout 1800 ./build/videoMetadataPlayer long120.ts
```

**Phải:** không crash, không rò rỉ bộ nhớ (xem `top`).

---

## 10. Test định dạng video khác

| Định dạng | Lệnh tạo | Kỳ vọng |
|---|---|---|
| MP4 thật | `ffmpeg -i video.mp4 -c copy real.mp4` | mở được, mã vạch đọc được |
| MKV | `ffmpeg -i video.mp4 -c copy real.mkv` | mở được |
| 720p | `ffmpeg -i video.mp4 -vf scale=1280:720 -c copy s720.mp4` | |
| 1080p | `ffmpeg -i video.mp4 -vf scale=1920:1080 -c copy s1080.mp4` | mã vạch đọc 100% |
| H.265 | `ffmpeg -i video.mp4 -c copy h265.ts` (nếu có libx265) | |

**Với MKV và MP4:** mục lục không cần thiết (đã có sẵn index trong container) → hành vi y như cũ.

**Với 1080p:** theo hồ sơ cũ mục 3.2, mã vạch đọc được 100% (module rộng 2,0 px).

---

## 11. Test file 2 tiếng

### 11.1 Mở và quét

```bash
./build/videoMetadataPlayer long120.ts
```

**Kỳ vọng:** mở được ngay, mục lục quét nền, dòng "Indexing 47%" hiện rồi tắt.

### 11.2 Tua xa khi mục lục chưa xong

Ngay sau khi mở, tua tới 90 phút.

**Kỳ vọng:** seek vẫn hoạt động (dùng hành vi cũ vì mục lục chưa tới), không treo.

### 11.3 Tua xa sau khi mục lục xong

**Kỳ vọng:** nhanh, CPU thấp.

---

## 12. Bảng tổng kết

Anh điền sau khi test:

| Hạng mục | Kỳ vọng | Đã đo | Đạt? |
|---|---|---|---|
| Build sạch, không lỗi | ✅ | | |
| Số keyframe khớp ffprobe | 28 = 28 | | |
| Buffer/seek khi bật index | ~117 (từ ~642) | | |
| Thời lượng file 2h | 120:00.898, cố định | | |
| Seek chính xác đúng ID | 8/8 | | |
| Hạ cánh đúng hình | ✅ | | |
| CPU khi tua giảm | ~5 lần | | |
| Overlay đầy đủ trường | ✅ | | |
| Cắt video: số dòng CSV = số frame | ✅ | | |
| Cắt video: từng dòng khớp từng frame | ✅ | | |
| Đồ thị pan/tilt hiện và chạy | ✅ | | |
| TSan không có lỗi thật | ✅ | | |
| Không crash sau 30 phút | ✅ | | |

---

## 13. Những điều **không** cần test

| Việc | Vì sao |
|---|---|
| Frame 3459, 3461 bị rớt | Đã biết từ dữ liệu mẫu, không phải lỗi |
| CSV dư 53 dòng | Đã biết, không phải lỗi |
| Tọa độ bằng 0 hoặc `-214.748365` | Đã biết, không vẽ bản đồ |
| File 2h có ID lặp | Đã biết, dùng để đo |
