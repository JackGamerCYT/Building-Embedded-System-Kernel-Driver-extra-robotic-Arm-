# ĐỒ ÁN TAY GẮP ROBOT PHÂN LOẠI KHỐI MÀU TỰ ĐỘNG (STEPPER MOTOR)
## ESP32-CAM • Raspberry Pi Zero 2 W • Linux Platform Driver

Dự án này là hệ thống điều khiển tay gắp robot 3-DOF + Gripper sử dụng động cơ bước (Stepper Motor) điều khiển bằng tín hiệu phát xung (STEP/DIR) trực tiếp qua Linux Driver trên Raspberry Pi Zero 2 W. ESP32-CAM thực hiện xử lý ảnh vùng ROI cố định để phát hiện vật thể và truyền thông tin qua mạng LAN không dây (HTTP POST) tới dịch vụ máy trạng thái (FSM) ở user-space (`robotd`).

---

## I. KIẾN TRÚC HỆ THỐNG & RANH GIỚI TRÁCH NHIỆM

Hệ thống được thiết kế chia lớp chặt chẽ để đảm bảo tính an toàn thời gian thực:
1. **ESP32-CAM**: Chụp ảnh vùng máng pickup cố định, so sánh độ chênh lệch điểm ảnh (difference), phát sự kiện `OBJECT_PRESENT` kèm theo số chuỗi `seq` qua mạng không dây, sau đó chờ phản hồi ACK (accepted/duplicate/busy).
2. **Userspace Daemon (`robotd`)**: Tiến trình nền chạy máy trạng thái hữu hạn (FSM) gồm các trạng thái: `BOOT`, `SELF_TEST`, `IDLE`, `PICKING`, `PLACING`, `COMPLETE`, `ERROR`, `E_STOP`. Nhận gói tin từ camera, chống trùng lặp, quản lý thời gian chờ (timeout), ghi nhận bộ đếm có tính chất idempotent và lưu trữ bền vững số lượng gắp thành công.
3. **Linux Kernel Driver (`robot_arm_step`)**: Platform driver đăng ký thành character device `/dev/robot_arm` dạng `miscdevice` (mở độc quyền). Trực tiếp quản lý GPIO, phát xung điều khiển motor bước, tiếp nhận sự kiện ngắt từ 3 cảm biến (Pickup, Grip, Place) thông qua GPIO Descriptor API, đẩy vào hàng đợi `kfifo`, và thực hiện cơ chế dừng khẩn cấp E-STOP cực nhanh bằng phần cứng lẫn ngắt phần mềm để ngắt nguồn cuộn dây động cơ.

---

## II. BẢN ĐỒ CHÂN KẾT NỐI (PIN MAP) TRÊN RASPBERRY PI
| Tín hiệu | GPIO Pi | Chân Driver A4988 / Sensor | Mô tả kỹ thuật |
| :--- | :--- | :--- | :--- |
| **STEP 0** | GPIO5 | STEP (Joint 0 - Đế) | Chân phát xung di chuyển khớp xoay đế |
| **DIR 0** | GPIO6 | DIR (Joint 0 - Đế) | Hướng xoay khớp đế |
| **STEP 1** | GPIO12 | STEP (Joint 1 - Vai) | Chân phát xung di chuyển khớp vai |
| **DIR 1** | GPIO13 | DIR (Joint 1 - Vai) | Hướng xoay khớp vai |
| **STEP 2** | GPIO16 | STEP (Joint 2 - Khuỷu) | Chân phát xung di chuyển khớp khuỷu |
| **DIR 2** | GPIO19 | DIR (Joint 2 - Khuỷu) | Hướng xoay khớp khuỷu |
| **STEP 3** | GPIO26 | STEP (Joint 3 - Gripper) | Chân phát xung đóng/mở kẹp |
| **DIR 3** | GPIO21 | DIR (Joint 3 - Gripper) | Hướng đóng/mở kẹp |
| **EN GLOBAL**| GPIO20 | EN (Chân Enable chung 4 driver)| **Active-Low**: Mức LOW để cấp điện cuộn dây |
| **Pickup** | GPIO17 | Out (Cảm biến máng gắp) | Active-Low (Có vật = LOW), có chống rung |
| **Grip** | GPIO27 | Out (Cảm biến kẹp) | Active-Low (Đang giữ vật = LOW) |
| **Place** | GPIO22 | Out (Cảm biến khay nhận) | Active-Low (Vật đã rơi vào khay = LOW) |
| **E-STOP** | GPIO24 | Tiếp điểm thường đóng (NC) | Ngắt điện và báo tín hiệu ngắt khẩn cấp |
| **Ready LED**| GPIO23 | LED chỉ báo trạng thái | Sáng khi hệ thống IDLE / Sẵn sàng |
| **PCA9685 OE**| GPIO25 | Chân OE dự phòng | Kích HIGH để disable nhanh (nếu có dùng thêm servo) |

### *Lưu ý an toàn quan trọng*:
- **Tuyệt đối không cấp nguồn động cơ bước từ Raspberry Pi**. Cấp nguồn DC 12V 5A riêng cho động cơ bước vào chân VMOT/GND của A4988. Chân VDD/GND của A4988 kết nối với nguồn 3.3V/GND của Raspberry Pi.
- **Nối chung Mass tín hiệu**: GND của nguồn 12V và GND của Raspberry Pi bắt buộc phải được đấu nối chung tại một điểm (sơ đồ hình sao) để tránh sụt áp gây reset Pi.
- Mọi ngõ vào GPIO của Raspberry Pi chỉ được nhận mức logic tối đa là 3.3V. Đối với cảm biến tiệm cận 5V, bắt buộc dùng mạch phân áp điện trở hoặc level shifter để hạ tín hiệu xuống 3.3V trước khi nối vào Pi.

---

## III. HƯỚNG DẪN CÀI ĐẶT & BIÊN DỊCH

### 1. Chuẩn bị hệ điều hành và Headers
Hệ điều hành yêu cầu **Raspberry Pi OS Lite 64-bit** (Headless) để tối ưu hiệu năng. Tiến hành khóa cập nhật tự động kernel sau khi cài đặt thành công kernel headers khớp với phiên bản kernel đang hoạt động:
```bash
# Xem phiên bản kernel hiện tại
uname -a

# Cài đặt công cụ biên dịch và kernel headers
sudo apt update
sudo apt install -y git build-essential i2c-tools device-tree-compiler linux-headers-rpi-v8

# Kiểm tra thư mục liên kết build kernel
test -e /lib/modules/$(uname -r)/build && echo "Kernel Headers OK!"
```

### 2. Biên dịch và nạp Device Tree Overlay
```bash
# Biên dịch file DTS sang nhị phân DTBO
dtc -@ -I dts -O dtb -o robot-arm.dtbo robot-arm-overlay.dts

# Copy file dtbo vào phân vùng nạp boot overlays
sudo cp robot-arm.dtbo /boot/overlays/

# Đăng ký overlay tại cấu hình hệ thống
echo "dtoverlay=robot-arm" | sudo tee -a /boot/config.txt
sudo reboot
```

### 3. Biên dịch Driver nhân (Kernel Module)
```bash
cd kernel/robot_arm
make

# Nạp Driver vào nhân Linux
sudo insmod robot_arm_step.ko

# Kiểm tra log nhân Linux và file thiết bị tự động tạo
dmesg | tail -n 30
ls -l /dev/robot_arm
```
*Ghi chú: Nếu hệ thống báo "Invalid module format", hãy đối chiếu phiên bản headers bằng lệnh `modinfo robot_arm_step.ko` và biên dịch lại trên đúng phiên bản kernel đang chạy.*

### 4. Biên dịch và cài đặt Ứng dụng điều khiển
```bash
# Biên dịch CLI kiểm thử robotctl
cd userspace/robotctl
make
sudo ./robotctl status
sudo ./robotctl enable
sudo ./robotctl step 0 200 1 1000  # Quay khớp 0 (đế) 200 bước, hướng 1, trễ 1000us

# Biên dịch và chạy daemon FSM tự động robotd
cd ../robotd
make
sudo ./robotd
```

---

## IV. QUY TRÌNH QUẢN LÝ TIẾN ĐỘ & NỘP BÀI UTEX
Để tuân thủ yêu cầu học tập của bộ môn Thiết kế Hệ thống Nhúng tại **HCMUTE**:
1. Giải nén toàn bộ mã nguồn vào thư mục làm việc của bạn.
2. Khởi tạo Git kho chứa:
   ```bash
   git init
   git config --global user.name "Họ và Tên Sinh Viên"
   git config --global user.email "mssv@student.hcmute.edu.vn"
   git add .
   git commit -m "feat: khởi tạo project tay gắp robot động cơ bước hoàn chỉnh"
   git tag -a stepper-baseline -m "Phiên bản baseline hoàn thiện thiết kế phần cứng và driver"
   ```
3. Tạo repository trên GitHub và kết nối:
   ```bash
   git remote add origin https://github.com/username/robot-arm-stepper.git
   git branch -M main
   git push -u origin main --tags
   ```
4. Cuối mỗi tuần phát triển, thực hiện commit kèm tag tương ứng (ví dụ: `stepper-w2-gate`, `stepper-w3-gate`...) làm bằng chứng nghiệm thu tiến độ và nộp link GitHub lên hệ thống **UTEX**.
