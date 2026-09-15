#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/timer.h>

static struct input_dev *vjoy_dev;
static struct timer_list vjoy_timer;
static int button_state = 0; // 0 = Nhả, 1 = Nhấn

// Hàm này sẽ tự động chạy mỗi 2 giây (đóng vai trò như ngón tay bấm nút)
static void vjoy_timer_cb(struct timer_list *t) {
    button_state = !button_state; // Đảo trạng thái nút
    
    // Bơm sự kiện nhấn/nhả nút (BTN_TRIGGER) vào hệ thống
    input_report_key(vjoy_dev, BTN_TRIGGER, button_state);
    input_sync(vjoy_dev); // Bắt buộc phải có để chốt sự kiện
    
    pr_info("Virtual Joy: Nut bam dang %s\n", button_state ? "NHAN" : "NHA");
    
    // Lên lịch chạy lại hàm này sau 2 giây (2000 ms)
    mod_timer(&vjoy_timer, jiffies + msecs_to_jiffies(2000));
}

static int __init vjoy_init(void) {
    int error;

    // 1. Cấp phát bộ nhớ cho thiết bị Input
    vjoy_dev = input_allocate_device();
    if (!vjoy_dev) return -ENOMEM;

    // 2. Đặt tên hiển thị trong hệ thống
    vjoy_dev->name = "Tay Gap Robot - Virtual Joystick";
    vjoy_dev->id.bustype = BUS_VIRTUAL; // Khai báo đây là thiết bị ảo

    // 3. Phân quyền: Cấp phép cho thiết bị này có khả năng gửi sự kiện phím bấm (EV_KEY) và cụ thể là nút Cò (BTN_TRIGGER)
    set_bit(EV_KEY, vjoy_dev->evbit);
    set_bit(BTN_TRIGGER, vjoy_dev->keybit);

    // 4. Đăng ký thiết bị với Input Subsystem của Linux
    error = input_register_device(vjoy_dev);
    if (error) {
        input_free_device(vjoy_dev);
        return error;
    }

    // 5. Khởi động Timer giả lập phần cứng
    timer_setup(&vjoy_timer, vjoy_timer_cb, 0);
    mod_timer(&vjoy_timer, jiffies + msecs_to_jiffies(2000));

    pr_info("Virtual Joy: Da khoi tao thanh cong!\n");
    return 0;
}

static void __exit vjoy_exit(void) {
   timer_delete_sync(&vjoy_timer);// Tắt timer trước
    input_unregister_device(vjoy_dev); // Gỡ thiết bị
    pr_info("Virtual Joy: Da go driver.\n");
}

module_init(vjoy_init);
module_exit(vjoy_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vo Nhat Nam");