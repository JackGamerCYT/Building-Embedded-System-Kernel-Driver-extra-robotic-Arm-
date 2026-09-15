#include <linux/module.h>
#include <linux/i2c.h>     // Thư viện cốt lõi chứa các cấu trúc i2c_driver, i2c_client

// =========================================================================
// 1. HÀM PROBE (Chạy khi Kernel phát hiện phần cứng cắm vào mạch)
// =========================================================================
static int my_i2c_probe(struct i2c_client *client) {
    // client->addr chứa địa chỉ I2C thực tế của thiết bị (ví dụ 0x68 của DS1307)
    pr_info("Bus Driver: Phat hien thiet bi I2C tai dia chi 0x%x\n", client->addr);
    
    // Nơi đây bạn sẽ viết code:
    // - Đọc thanh ghi ID của cảm biến để kiểm tra xem có đúng đồ thật không
    // - Khởi tạo cấu hình ban đầu cho cảm biến
    
    return 0; // Trả về 0 báo hiệu Probe thành công, Kernel sẽ chốt kết nối
}

// =========================================================================
// 2. HÀM REMOVE (Chạy khi rút thiết bị ra khỏi mạch hoặc gỡ module)
// =========================================================================
static void my_i2c_remove(struct i2c_client *client) {
    pr_info("Bus Driver: Thiet bi I2C tai dia chi 0x%x da bi ngat ket noi\n", client->addr);
    
    // Nơi đây bạn viết code để tắt nguồn cảm biến, giải phóng RAM
}

// =========================================================================
// 3. DANH SÁCH THIẾT BỊ HỖ TRỢ (Match Table)
// =========================================================================
static const struct i2c_device_id my_i2c_id[] = {
    { "taygap_sensor", 0 }, // Tên định danh để Kernel dò tìm trong Device Tree
    { "ds1307", 0 },        // Bạn có thể thêm nhiều thiết bị vào danh sách này
    { }                     // Bắt buộc phải có một dòng rỗng ở cuối để chốt danh sách
};
MODULE_DEVICE_TABLE(i2c, my_i2c_id); // Đăng ký danh sách này với hệ điều hành

// =========================================================================
// 4. KHAI BÁO CẤU TRÚC I2C DRIVER CHUẨN
// =========================================================================
static struct i2c_driver my_i2c_driver = {
    .driver = {
        .name = "taygap_i2c_bus_driver", // Tên của driver
        .owner = THIS_MODULE,
    },
    .probe = my_i2c_probe,   // Chỉ định hàm cắm thiết bị
    .remove = my_i2c_remove, // Chỉ định hàm rút thiết bị
    .id_table = my_i2c_id,   // Gắn danh sách thiết bị hỗ trợ vào
};

// =========================================================================
// 5. ĐĂNG KÝ VỚI KERNEL
// =========================================================================
// Lệnh này là một "phép thuật" của Linux (Macro). Nó tự động sinh ra hàm 
// module_init() và module_exit() cho bạn, giúp code ngắn gọn hơn rất nhiều.
module_i2c_driver(my_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vo Nhat Nam");