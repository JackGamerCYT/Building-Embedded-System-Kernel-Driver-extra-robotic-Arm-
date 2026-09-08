#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#define DRIVER_NAME "spkt_gpio_i2c_driver"
#define DRIVER_CLASS "spkt_class"

// --- [PHẦN 1: CẤU HÌNH GPIO (LED)] ---
#define GPIO_BASE 0xFE200000 
#define GPFSEL0   0x00  
#define GPSET0    0x1C  
#define GPCLR0    0x28  

static dev_t dev_num;
static struct class *driver_class;
static struct cdev driver_cdev;
void __iomem *gpio_registers; 

static void gpio_on(void) { iowrite32(1 << 0, gpio_registers + GPSET0); }
static void gpio_off(void) { iowrite32(1 << 0, gpio_registers + GPCLR0); }

// --- [PHẦN 2: KHUNG GIAO TIẾP I2C CHO DS1307] ---
static struct i2c_client *ds1307_client; 

// Đã cập nhật: Hàm probe ở Kernel mới chỉ nhận 1 tham số
static int ds1307_probe(struct i2c_client *client) {
    printk("DS1307 I2C: Da tim thay thiet bi o dia chi 0x%x\n", client->addr);
    ds1307_client = client;
    return 0;
}

// Đã cập nhật: Hàm remove ở Kernel mới đổi kiểu trả về thành void
static void ds1307_remove(struct i2c_client *client) {
    printk("DS1307 I2C: Da ngat ket noi thiet bi\n");
}

// Bảng nhận diện thiết bị I2C
static const struct i2c_device_id ds1307_id[] = {
    { "ds1307", 0 }, 
    { }
};
MODULE_DEVICE_TABLE(i2c, ds1307_id);

// Đóng gói hồ sơ i2c_driver
static struct i2c_driver ds1307_driver = {
    .driver = {
        .name = "ds1307_spkt_driver",
        .owner = THIS_MODULE,
    },
    .probe = ds1307_probe,
    .remove = ds1307_remove,
    .id_table = ds1307_id,
};

// --- [PHẦN 3: FILE OPERATIONS (GIAO TIẾP USER SPACE)] ---
static ssize_t driver_read(struct file *File, char __user *user_buf, size_t count, loff_t *offs) {
    return 0; 
}

static ssize_t driver_write(struct file *File, const char __user *user_buf, size_t count, loff_t *offs) {
    char buf[10] = {0};
    if (copy_from_user(buf, user_buf, count)) return -EFAULT;

    if (strncmp(buf, "on", 2) == 0) {
        printk("Ra lenh BAT LED!\n");
        gpio_on();
    } else if (strncmp(buf, "off", 3) == 0) {
        printk("Ra lenh TAT LED!\n");
        gpio_off();
    } else {
        printk("Lenh khong hop le.\n");
    }
    return count;
}

static int driver_open(struct inode *device_file, struct file *instance) { return 0; }
static int driver_close(struct inode *device_file, struct file *instance) { return 0; }

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = driver_open,
    .release = driver_close,
    .read = driver_read,
    .write = driver_write
};

// --- [PHẦN 4: HÀM KHỞI TẠO VÀ KẾT THÚC] ---
static int __init my_driver_init(void) {
    printk("Dang nap SPKT GPIO & I2C Driver...\n");

    // 1. Đăng ký GPIO
    if (alloc_chrdev_region(&dev_num, 0, 1, DRIVER_NAME) < 0) return -1;
    if ((driver_class = class_create(DRIVER_CLASS)) == NULL) goto r_class;
    if (device_create(driver_class, NULL, dev_num, NULL, DRIVER_NAME) == NULL) goto r_device;
    
    cdev_init(&driver_cdev, &fops);
    if (cdev_add(&driver_cdev, dev_num, 1) < 0) goto r_device;

    gpio_registers = ioremap(GPIO_BASE, PAGE_SIZE);
    if (gpio_registers == NULL) goto r_device;
    iowrite32(0x01, gpio_registers + GPFSEL0); 

    // 2. Đăng ký I2C Driver
    if (i2c_add_driver(&ds1307_driver) != 0) {
        printk("Loi: Khong the dang ky I2C Driver\n");
        goto r_i2c;
    }

    printk("SPKT Driver nap thanh cong!\n");
    return 0;

r_i2c:
    iounmap(gpio_registers);
r_device:
    class_destroy(driver_class);
r_class:
    unregister_chrdev_region(dev_num, 1);
    return -1;
}

static void __exit my_driver_exit(void) {
    gpio_off();
    iounmap(gpio_registers);
    
    i2c_del_driver(&ds1307_driver);

    cdev_del(&driver_cdev);
    device_destroy(driver_class, dev_num);
    class_destroy(driver_class);
    unregister_chrdev_region(dev_num, 1);

    printk("SPKT Driver da go bo thanh cong!\n");
}

module_init(my_driver_init);
module_exit(my_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nhom SPKT");
MODULE_DESCRIPTION("Driver dieu khien LED GPIO & DS1307 I2C cho Raspberry Pi");