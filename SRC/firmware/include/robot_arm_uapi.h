#ifndef _UAPI_LINUX_ROBOT_ARM_H
#define _UAPI_LINUX_ROBOT_ARM_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define RA_ABI_VERSION     2  /* Phiên bản ABI cho mô hình Stepper Motor */

/* Định nghĩa các kiểu sự kiện từ cảm biến ngắt */
#define RA_EVENT_PICKUP    1
#define RA_EVENT_GRIP      2
#define RA_EVENT_PLACE     3
#define RA_EVENT_ESTOP     4

/* Trạng thái máy FSM đồng bộ */
#define RA_STATE_BOOT         0
#define RA_STATE_SELF_TEST    1
#define RA_STATE_IDLE         2
#define RA_STATE_PICKING      3
#define RA_STATE_PLACING      4
#define RA_STATE_COMPLETE     5
#define RA_STATE_ERROR        6
#define RA_STATE_ESTOP        7

/* Cấu trúc truyền sự kiện từ nhân lên user-space */
struct ra_event {
    __u64 timestamp_ns;
    __u32 event_type;
    __u32 value;  /* 0 = Nhả, 1 = Kích hoạt */
    __u32 reserved;
};

/* Cấu trúc lấy trạng thái hệ thống */
struct ra_status {
    __u32 state;
    __u32 count;
    __u32 target;
    __u32 fault_bits;
    __u32 estop_latched;
    __u32 last_seq_rx;
};

/* Cấu trúc ra lệnh di chuyển khớp động cơ bước */
struct ra_step_cmd {
    __u32 joint_id;   /* 0: Đế, 1: Vai, 2: Khuỷu, 3: Kẹp */
    __u32 steps;      /* Số bước cần di chuyển */
    __u32 direction;  /* 0: Ngược chiều, 1: Thuận chiều */
    __u32 delay_us;   /* Thời gian trễ giữa các xung phát (tốc độ) */
};

/* Định nghĩa các lệnh điều khiển IOCTL sử dụng kiểu dữ liệu kích thước cố định */
#define RA_IOC_MAGIC  'R'

#define RA_IOC_GET_ABI          _IOR(RA_IOC_MAGIC, 1, __u32)
#define RA_IOC_ENABLE           _IO(RA_IOC_MAGIC, 2)
#define RA_IOC_DISABLE          _IO(RA_IOC_MAGIC, 3)
#define RA_IOC_STEP_JOINT       _IOW(RA_IOC_MAGIC, 4, struct ra_step_cmd)
#define RA_IOC_GET_STATUS       _IOR(RA_IOC_MAGIC, 5, struct ra_status)
#define RA_IOC_SET_TARGET       _IOW(RA_IOC_MAGIC, 6, __u32)
#define RA_IOC_COMMIT_SUCCESS   _IOW(RA_IOC_MAGIC, 7, __u32)  /* Tham số: cycle_id */
#define RA_IOC_RESTORE_COUNT    _IOW(RA_IOC_MAGIC, 8, __u32)  /* Khôi phục bộ đếm từ log */
#define RA_IOC_CLEAR_FAULT      _IO(RA_IOC_MAGIC, 9)

/* Định nghĩa cờ báo lỗi fault_bits */
#define RA_FAULT_ESTOP      (1 << 0)
#define RA_FAULT_TIMEOUT    (1 << 1)
#define RA_FAULT_SENSOR     (1 << 2)
#define RA_FAULT_SPI_I2C    (1 << 3)

#endif /* _UAPI_LINUX_ROBOT_ARM_H */
