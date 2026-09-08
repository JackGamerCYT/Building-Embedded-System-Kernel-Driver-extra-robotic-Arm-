#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "robot_arm_uapi.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sinh vien HCMUTE");
MODULE_DESCRIPTION("Linux Platform Driver for Stepper Motor Robotic Arm");
MODULE_VERSION("2.0");

#define FIFO_SIZE   32
#define NUM_JOINTS  4

/* Cấu trúc dữ liệu thiết bị */
struct robot_arm_dev {
    struct miscdevice misc_dev;
    struct mutex ioctl_lock;
    spinlock_t lock;
    
    /* Chân GPIO Descriptors */
    struct gpio_desc *gpiod_step[NUM_JOINTS];
    struct gpio_desc *gpiod_dir[NUM_JOINTS];
    struct gpio_desc *gpiod_enable_global;
    struct gpio_desc *gpiod_sensor_pickup;
    struct gpio_desc *gpiod_sensor_grip;
    struct gpio_desc *gpiod_sensor_place;
    struct gpio_desc *gpiod_sensor_estop;
    struct gpio_desc *gpiod_led_ready;
    struct gpio_desc *gpiod_pca_oe;
    
    /* IRQ Numbers */
    int irq_pickup;
    int irq_grip;
    int irq_place;
    int irq_estop;
    
    /* Hàng đợi và đồng bộ sự kiện */
    DECLARE_KFIFO(event_fifo, struct ra_event, FIFO_SIZE);
    wait_queue_head_t read_wait;
    
    /* Biến trạng thái runtime */
    bool opened;
    bool enabled;
    u32 count;
    u32 target;
    u32 fault_bits;
    bool estop_latched;
    u32 current_state;
};

static struct robot_arm_dev *g_dev = NULL;

/* Hàm hỗ trợ phát xung dịch chuyển động cơ bước */
static int step_motor_move(struct robot_arm_dev *dev, struct ra_step_cmd *cmd) {
    int i;
    if (cmd->joint_id >= NUM_JOINTS)
        return -EINVAL;
        
    if (!dev->enabled || dev->estop_latched)
        return -ECANCELED;
        
    /* Cấu hình hướng xoay */
    gpiod_set_value(dev->gpiod_dir[cmd->joint_id], cmd->direction);
    udelay(5);
    
    /* Phát chuỗi xung STEP */
    for (i = 0; i < cmd->steps; i++) {
        if (!dev->enabled || dev->estop_latched)
            return -ECANCELED;
            
        gpiod_set_value(dev->gpiod_step[cmd->joint_id], 1);
        udelay(2);
        gpiod_set_value(dev->gpiod_step[cmd->joint_id], 0);
        
        if (cmd->delay_us >= 1000)
            msleep(cmd->delay_us / 1000);
        else
            udelay(cmd->delay_us);
    }
    return 0;
}

/* Xử lý ngắt khẩn cấp E-STOP (Hard IRQ + Threaded IRQ) */
static irqreturn_t estop_hard_handler(int irq, void *dev_id) {
    struct robot_arm_dev *dev = dev_id;
    /* Cắt điện cuộn dây động cơ ngay lập tức ở mức phần cứng cực nhanh */
    if (dev->gpiod_enable_global) {
        gpiod_set_value(dev->gpiod_enable_global, 1); /* Active-low: 1 là ngắt dòng */
    }
    dev->enabled = false;
    dev->estop_latched = true;
    dev->fault_bits |= RA_FAULT_ESTOP;
    dev->current_state = RA_STATE_ESTOP;
    
    return IRQ_WAKE_THREAD;
}

static irqreturn_t estop_thread_handler(int irq, void *dev_id) {
    struct robot_arm_dev *dev = dev_id;
    struct ra_event ev;
    unsigned long flags;
    
    ev.timestamp_ns = ktime_get_ns();
    ev.event_type = RA_EVENT_ESTOP;
    ev.value = 1;
    
    spin_lock_irqsave(&dev->lock, flags);
    kfifo_put(&dev->event_fifo, ev);
    spin_unlock_irqrestore(&dev->lock, flags);
    
    wake_up_interruptible(&dev->read_wait);
    dev_err(&g_dev->misc_dev.this_device->parent, "EMERGENCY ESTOP TRIGGERED BY HARDWARE INTERRUPT!\n");
    return IRQ_HANDLED;
}

/* Threaded IRQ Handler cho cảm biến với chống rung phần mềm */
static irqreturn_t sensor_thread_handler(int irq, void *dev_id) {
    struct robot_arm_dev *dev = dev_id;
    struct ra_event ev;
    unsigned long flags;
    int val = 0;
    
    msleep(20); /* Trễ chống rung phím nhấn cơ khí */
    
    ev.timestamp_ns = ktime_get_ns();
    if (irq == dev->irq_pickup) {
        ev.event_type = RA_EVENT_PICKUP;
        val = gpiod_get_value(dev->gpiod_sensor_pickup);
    } else if (irq == dev->irq_grip) {
        ev.event_type = RA_EVENT_GRIP;
        val = gpiod_get_value(dev->gpiod_sensor_grip);
    } else if (irq == dev->irq_place) {
        ev.event_type = RA_EVENT_PLACE;
        val = gpiod_get_value(dev->gpiod_sensor_place);
    } else {
        return IRQ_NONE;
    }
    
    ev.value = val ? 1 : 0;
    
    spin_lock_irqsave(&dev->lock, flags);
    if (!kfifo_is_full(&dev->event_fifo)) {
        kfifo_put(&dev->event_fifo, ev);
    }
    spin_unlock_irqrestore(&dev->lock, flags);
    
    wake_up_interruptible(&dev->read_wait);
    return IRQ_HANDLED;
}

/* Các thao tác File Operations cơ bản */
static int robot_arm_open(struct inode *inode, struct file *file) {
    struct robot_arm_dev *dev = g_dev;
    
    mutex_lock(&dev->ioctl_lock);
    if (dev->opened) {
        mutex_unlock(&dev->ioctl_lock);
        return -EBUSY; /* Chế độ mở độc quyền chống tranh chấp phần cứng */
    }
    dev->opened = true;
    mutex_unlock(&dev->ioctl_lock);
    
    nonseekable_open(inode, file);
    return 0;
}

static int robot_arm_release(struct inode *inode, struct file *file) {
    struct robot_arm_dev *dev = g_dev;
    
    mutex_lock(&dev->ioctl_lock);
    dev->opened = false;
    dev->enabled = false;
    /* Đảm bảo ngắt điện cuộn dây khi tắt app để tránh nóng Driver */
    if (dev->gpiod_enable_global) {
        gpiod_set_value(dev->gpiod_enable_global, 1);
    }
    kfifo_reset(&dev->event_fifo);
    mutex_unlock(&dev->ioctl_lock);
    
    return 0;
}

static ssize_t robot_arm_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    struct robot_arm_dev *dev = g_dev;
    unsigned int copied;
    int ret;
    
    if (count < sizeof(struct ra_event))
        return -EINVAL;
        
    if (kfifo_is_empty(&dev->event_fifo)) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
            
        ret = wait_event_interruptible(dev->read_wait, !kfifo_is_empty(&dev->event_fifo) || dev->estop_latched);
        if (ret)
            return ret;
    }
    
    mutex_lock(&dev->ioctl_lock);
    ret = kfifo_to_user(&dev->event_fifo, buf, sizeof(struct ra_event), &copied);
    mutex_unlock(&dev->ioctl_lock);
    
    return ret ? ret : copied;
}

static __poll_t robot_arm_poll(struct file *file, poll_table *wait) {
    struct robot_arm_dev *dev = g_dev;
    __poll_t mask = 0;
    
    poll_wait(file, &dev->read_wait, wait);
    
    if (!kfifo_is_empty(&dev->event_fifo))
        mask |= EPOLLIN | POLLRDNORM;
        
    if (dev->estop_latched)
        mask |= EPOLLERR;
        
    return mask;
}

static long robot_arm_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct robot_arm_dev *dev = g_dev;
    long ret = 0;
    
    if (_IOC_TYPE(cmd) != RA_IOC_MAGIC)
        return -ENOTTY;
        
    mutex_lock(&dev->ioctl_lock);
    
    switch (cmd) {
        case RA_IOC_GET_ABI: {
            u32 abi = RA_ABI_VERSION;
            if (copy_to_user((void __user *)arg, &abi, sizeof(abi)))
                ret = -EFAULT;
            break;
        }
        case RA_IOC_ENABLE:
            if (dev->estop_latched) {
                ret = -EPERM; /* Không cho phép ENABLE nếu E-STOP phần cứng chưa nhả */
            } else {
                dev->enabled = true;
                if (dev->gpiod_enable_global)
                    gpiod_set_value(dev->gpiod_enable_global, 0); /* LOW: ENABLE */
                dev->current_state = RA_STATE_IDLE;
            }
            break;
        case RA_IOC_DISABLE:
            dev->enabled = false;
            if (dev->gpiod_enable_global)
                gpiod_set_value(dev->gpiod_enable_global, 1); /* HIGH: DISABLE */
            dev->current_state = RA_STATE_BOOT;
            break;
        case RA_IOC_STEP_JOINT: {
            struct ra_step_cmd sc;
            if (copy_from_user(&sc, (void __user *)arg, sizeof(sc))) {
                ret = -EFAULT;
                break;
            }
            ret = step_motor_move(dev, &sc);
            break;
        }
        case RA_IOC_GET_STATUS: {
            struct ra_status stat;
            stat.state = dev->current_state;
            stat.count = dev->count;
            stat.target = dev->target;
            stat.fault_bits = dev->fault_bits;
            stat.estop_latched = dev->estop_latched ? 1 : 0;
            stat.last_seq_rx = 0;
            if (copy_to_user((void __user *)arg, &stat, sizeof(stat)))
                ret = -EFAULT;
            break;
        }
        case RA_IOC_SET_TARGET: {
            u32 tgt;
            if (copy_from_user(&tgt, (void __user *)arg, sizeof(tgt))) {
                ret = -EFAULT;
                break;
            }
            dev->target = tgt;
            break;
        }
        case RA_IOC_COMMIT_SUCCESS: {
            u32 cycle_id;
            if (copy_from_user(&cycle_id, (void __user *)arg, sizeof(cycle_id))) {
                ret = -EFAULT;
                break;
            }
            dev->count++;
            if (dev->target > 0 && dev->count >= dev->target) {
                dev->current_state = RA_STATE_COMPLETE;
            }
            break;
        }
        case RA_IOC_RESTORE_COUNT: {
            u32 rst;
            if (dev->current_state != RA_STATE_BOOT && dev->current_state != RA_STATE_ESTOP) {
                ret = -EPERM;
                break;
            }
            if (copy_from_user(&rst, (void __user *)arg, sizeof(rst))) {
                ret = -EFAULT;
                break;
            }
            dev->count = rst;
            break;
        }
        case RA_IOC_CLEAR_FAULT:
            /* Kiểm tra xem nút E-STOP vật lý đã được nhả chưa trước khi clear */
            if (dev->gpiod_sensor_estop && gpiod_get_value(dev->gpiod_sensor_estop) == 0) {
                ret = -EAGAIN; /* Vẫn chập E-STOP cơ khí */
            } else {
                dev->fault_bits = 0;
                dev->estop_latched = false;
                dev->current_state = RA_STATE_BOOT;
            }
            break;
        default:
            ret = -ENOTTY;
    }
    
    mutex_unlock(&dev->ioctl_lock);
    return ret;
}

static const struct file_operations robot_arm_fops = {
    .owner          = THIS_MODULE,
    .open           = robot_arm_open,
    .release        = robot_arm_release,
    .read           = robot_arm_read,
    .poll           = robot_arm_poll,
    .unlocked_ioctl = robot_arm_ioctl,
};

/* Platform Probe & Device Tree matching */
static int robot_arm_probe(struct platform_device *pdev) {
    struct device *dev = &pdev->dev;
    struct robot_arm_dev *rdev;
    int ret, i;
    
    rdev = devm_kzalloc(dev, sizeof(*rdev), GFP_KERNEL);
    if (!rdev)
        return -ENOMEM;
        
    rdev->misc_dev.minor = MISC_DYNAMIC_MINOR;
    rdev->misc_dev.name = "robot_arm";
    rdev->misc_dev.fops = &robot_arm_fops;
    
    mutex_init(&rdev->ioctl_lock);
    spin_lock_init(&rdev->lock);
    init_waitqueue_head(&rdev->read_wait);
    INIT_KFIFO(rdev->event_fifo);
    
    rdev->current_state = RA_STATE_BOOT;
    
    /* Ánh xạ các chân GPIO Descriptors từ Device Tree */
    for (i = 0; i < NUM_JOINTS; i++) {
        rdev->gpiod_step[i] = devm_gpiod_get_index(dev, "step", i, GPIOD_OUT_LOW);
        if (IS_ERR(rdev->gpiod_step[i])) return PTR_ERR(rdev->gpiod_step[i]);
        
        rdev->gpiod_dir[i] = devm_gpiod_get_index(dev, "dir", i, GPIOD_OUT_LOW);
        if (IS_ERR(rdev->gpiod_dir[i])) return PTR_ERR(rdev->gpiod_dir[i]);
    }
    
    rdev->gpiod_enable_global = devm_gpiod_get(dev, "enable-global", GPIOD_OUT_HIGH);
    if (IS_ERR(rdev->gpiod_enable_global)) return PTR_ERR(rdev->gpiod_enable_global);
    
    rdev->gpiod_sensor_pickup = devm_gpiod_get(dev, "sensor-pickup", GPIOD_IN);
    if (IS_ERR(rdev->gpiod_sensor_pickup)) return PTR_ERR(rdev->gpiod_sensor_pickup);
    
    rdev->gpiod_sensor_grip = devm_gpiod_get(dev, "sensor-grip", GPIOD_IN);
    if (IS_ERR(rdev->gpiod_sensor_grip)) return PTR_ERR(rdev->gpiod_sensor_grip);
    
    rdev->gpiod_sensor_place = devm_gpiod_get(dev, "sensor-place", GPIOD_IN);
    if (IS_ERR(rdev->gpiod_sensor_place)) return PTR_ERR(rdev->gpiod_sensor_place);
    
    rdev->gpiod_sensor_estop = devm_gpiod_get(dev, "sensor-estop", GPIOD_IN);
    if (IS_ERR(rdev->gpiod_sensor_estop)) return PTR_ERR(rdev->gpiod_sensor_estop);
    
    rdev->gpiod_led_ready = devm_gpiod_get_optional(dev, "led-ready", GPIOD_OUT_LOW);
    rdev->gpiod_pca_oe = devm_gpiod_get_optional(dev, "pca-oe", GPIOD_OUT_HIGH);
    
    /* Đăng ký sự kiện ngắt */
    rdev->irq_estop = gpiod_to_irq(rdev->gpiod_sensor_estop);
    ret = devm_request_threaded_irq(dev, rdev->irq_estop, estop_hard_handler, estop_thread_handler,
                                    IRQF_TRIGGER_FALLING, "ra-estop", rdev);
    if (ret) return ret;
    
    rdev->irq_pickup = gpiod_to_irq(rdev->gpiod_sensor_pickup);
    ret = devm_request_threaded_irq(dev, rdev->irq_pickup, NULL, sensor_thread_handler,
                                    IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "ra-pickup", rdev);
    if (ret) return ret;
    
    rdev->irq_grip = gpiod_to_irq(rdev->gpiod_sensor_grip);
    ret = devm_request_threaded_irq(dev, rdev->irq_grip, NULL, sensor_thread_handler,
                                    IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "ra-grip", rdev);
    if (ret) return ret;
    
    rdev->irq_place = gpiod_to_irq(rdev->gpiod_sensor_place);
    ret = devm_request_threaded_irq(dev, rdev->irq_place, NULL, sensor_thread_handler,
                                    IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "ra-place", rdev);
    if (ret) return ret;
    
    ret = misc_register(&rdev->misc_dev);
    if (ret) return ret;
    
    g_dev = rdev;
    platform_set_drvdata(pdev, rdev);
    dev_info(dev, "Robot Arm Stepper Driver probed successfully! Created /dev/robot_arm\n");
    return 0;
}

static int robot_arm_remove(struct platform_device *pdev) {
    struct robot_arm_dev *dev = platform_get_drvdata(pdev);
    
    misc_deregister(&dev->misc_dev);
    if (dev->gpiod_enable_global) {
        gpiod_set_value(dev->gpiod_enable_global, 1); /* Safe disable */
    }
    g_dev = NULL;
    return 0;
}

static const struct of_device_id robot_arm_of_match[] = {
    { .compatible = "hcmute,robot-arm-step", },
    { }
};
MODULE_DEVICE_TABLE(of, robot_arm_of_match);

static struct platform_driver robot_arm_driver = {
    .probe = robot_arm_probe,
    .remove = robot_arm_remove,
    .driver = {
        .name = "robot_arm_step",
        .of_match_table = robot_arm_of_match,
    },
};
module_platform_driver(robot_arm_driver);
