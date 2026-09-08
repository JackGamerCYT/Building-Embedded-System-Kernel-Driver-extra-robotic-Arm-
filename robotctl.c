#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "robot_arm_uapi.h"

void print_status(struct ra_status *stat) {
    printf("--- TRẠNG THÁI TAY GẮP ROBOT RUNTIME ---\n");
    switch (stat->state) {
        case RA_STATE_BOOT: printf("Trạng thái FSM: BOOT\n"); break;
        case RA_STATE_SELF_TEST: printf("Trạng thái FSM: SELF_TEST\n"); break;
        case RA_STATE_IDLE: printf("Trạng thái FSM: IDLE\n"); break;
        case RA_STATE_PICKING: printf("Trạng thái FSM: PICKING\n"); break;
        case RA_STATE_PLACING: printf("Trạng thái FSM: PLACING\n"); break;
        case RA_STATE_COMPLETE: printf("Trạng thái FSM: COMPLETE\n"); break;
        case RA_STATE_ERROR: printf("Trạng thái FSM: ERROR\n"); break;
        case RA_STATE_ESTOP: printf("Trạng thái FSM: EMERGENCY ESTOP LOCKED\n"); break;
        default: printf("Trạng thái FSM: KHÔNG XÁC ĐỊNH\n");
    }
    printf("Tổng chu trình thành công (Count): %u\n", stat->count);
    printf("Mục tiêu gắp (Target): %u\n", stat->target);
    printf("Cờ báo lỗi (Fault Bits): 0x%X\n", stat->fault_bits);
    if (stat->fault_bits & RA_FAULT_ESTOP) printf("  [!] LỖI: Dừng khẩn cấp E-STOP kích hoạt\n");
    if (stat->fault_bits & RA_FAULT_TIMEOUT) printf("  [!] LỖI: Quá thời gian chu trình (Timeout)\n");
    if (stat->fault_bits & RA_FAULT_SENSOR) printf("  [!] LỖI: Cảm biến báo sai chuỗi gắp\n");
    printf("Chốt khóa cứng E-STOP: %s\n", stat->estop_latched ? "ĐANG KHÓA (LATCHED)" : "BÌNH THƯỜNG");
}

int main(int argc, char *argv[]) {
    int fd;
    if (argc < 2) {
        printf("Sử dụng: %s <status|enable|disable|step|set_target|commit|clear_fault>\n", argv[0]);
        return 1;
    }
    
    fd = open("/dev/robot_arm", O_RDWR);
    if (fd < 0) {
        perror("Lỗi mở file /dev/robot_arm");
        return 1;
    }
    
    /* Kiểm tra phiên bản ABI trước để tránh lệch cấu trúc */
    u32 abi = 0;
    if (ioctl(fd, RA_IOC_GET_ABI, &abi) == 0) {
        if (abi != RA_ABI_VERSION) {
            fprintf(stderr, "LỖI LỆCH PHIÊN BẢN ABI: Driver v%u, CLI v%u\n", abi, RA_ABI_VERSION);
            close(fd);
            return 1;
        }
    }
    
    if (strcmp(argv[1], "status") == 0) {
        struct ra_status stat;
        if (ioctl(fd, RA_IOC_GET_STATUS, &stat) < 0) {
            perror("Lỗi ioctl status");
        } else {
            print_status(&stat);
        }
    } else if (strcmp(argv[1], "enable") == 0) {
        if (ioctl(fd, RA_IOC_ENABLE) < 0) {
            perror("Lỗi kích hoạt driver");
        } else {
            printf("Đã cấp nguồn cho Driver động cơ bước.\n");
        }
    } else if (strcmp(argv[1], "disable") == 0) {
        if (ioctl(fd, RA_IOC_DISABLE) < 0) {
            perror("Lỗi ngắt nguồn driver");
        } else {
            printf("Đã tắt nguồn cuộn dây động cơ bước (An toàn).\n");
        }
    } else if (strcmp(argv[1], "step") == 0) {
        if (argc < 6) {
            printf("Sử dụng: %s step <joint_id> <steps> <dir> <delay_us>\n", argv[0]);
            close(fd);
            return 1;
        }
        struct ra_step_cmd cmd;
        cmd.joint_id = atoi(argv[2]);
        cmd.steps = atoi(argv[3]);
        cmd.direction = atoi(argv[4]);
        cmd.delay_us = atoi(argv[5]);
        
        printf("Đang gửi lệnh quay Khớp %u: %u bước...\n", cmd.joint_id, cmd.steps);
        if (ioctl(fd, RA_IOC_STEP_JOINT, &cmd) < 0) {
            perror("Lỗi điều khiển di chuyển khớp động cơ");
        } else {
            printf("Quay thành công.\n");
        }
    } else if (strcmp(argv[1], "set_target") == 0) {
        if (argc < 3) {
            printf("Sử dụng: %s set_target <number>\n", argv[0]);
            close(fd);
            return 1;
        }
        u32 target = atoi(argv[2]);
        if (ioctl(fd, RA_IOC_SET_TARGET, &target) < 0) {
            perror("Lỗi đặt mục tiêu");
        } else {
            printf("Đã thiết lập mục tiêu gắp: %u khối gỗ.\n", target);
        }
    } else if (strcmp(argv[1], "clear_fault") == 0) {
        if (ioctl(fd, RA_IOC_CLEAR_FAULT) < 0) {
            perror("Lỗi xóa trạng thái lỗi");
        } else {
            printf("Đã xóa cờ lỗi và đưa robot về BOOT thành công.\n");
        }
    } else {
        printf("Lệnh không hợp lệ.\n");
    }
    
    close(fd);
    return 0;
}
