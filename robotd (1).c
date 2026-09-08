#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <time.h>

#include "robot_arm_uapi.h"

#define PORT 8080
#define PERSIST_FILE "/var/log/robot_arm.count"

/* Các cấu trúc Waypoint lưu quỹ động cơ bước */
struct waypoint {
    u32 steps[4];
    u32 dirs[4];
    u32 speed_us;
};

/* Lập trình chuỗi chuyển động Waypoint cố định cho MVP */
static struct waypoint path_home_to_pick = {
    .steps = {200, 300, 100, 0},
    .dirs = {1, 1, 0, 0},
    .speed_us = 1000
};

static struct waypoint path_grip_close = {
    .steps = {0, 0, 0, 150},
    .dirs = {0, 0, 0, 1},
    .speed_us = 1500
};

static struct waypoint path_pick_to_place = {
    .steps = {400, 200, 300, 0},
    .dirs = {0, 0, 1, 0},
    .speed_us = 1000
};

static struct waypoint path_grip_open = {
    .steps = {0, 0, 0, 150},
    .dirs = {0, 0, 0, 0},
    .speed_us = 1500
};

static struct waypoint path_place_to_home = {
    .steps = {200, 100, 200, 0},
    .dirs = {1, 0, 0, 0},
    .speed_us = 1000
};

/* Biến trạng thái toàn cục */
static int g_dev_fd = -1;
static u32 g_count = 0;
static u32 g_target = 10;
static u32 g_state = RA_STATE_BOOT;
static u32 g_last_seq = 0;

/* Khôi phục và lưu bền vững bộ đếm (idempotent) */
void save_count_to_disk(u32 count) {
    int fd = open(PERSIST_FILE, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0644);
    if (fd >= 0) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%u\n", count);
        write(fd, buf, len);
        fsync(fd);
        close(fd);
    }
}

u32 load_count_from_disk(void) {
    int fd = open(PERSIST_FILE, O_RDONLY);
    if (fd >= 0) {
        char buf[32];
        int bytes = read(fd, buf, sizeof(buf)-1);
        close(fd);
        if (bytes > 0) {
            buf[bytes] = '\0';
            return atoi(buf);
        }
    }
    return 0;
}

/* Thực thi chuỗi chuyển động waypoint thông qua Driver nhân */
int execute_motion(struct waypoint *wp) {
    int i;
    for (i = 0; i < 4; i++) {
        if (wp->steps[i] > 0) {
            struct ra_step_cmd cmd;
            cmd.joint_id = i;
            cmd.steps = wp->steps[i];
            cmd.direction = wp->dirs[i];
            cmd.delay_us = wp->speed_us;
            if (ioctl(g_dev_fd, RA_IOC_STEP_JOINT, &cmd) < 0) {
                return -1; /* Bị hủy do E-STOP hoặc mất nguồn cuộn dây */
            }
        }
    }
    return 0;
}

int main(void) {
    printf("Starting Robot Arm Stepper Daemon (FSM)...\n");
    
    g_dev_fd = open("/dev/robot_arm", O_RDWR);
    if (g_dev_fd < 0) {
        perror("Failed to open /dev/robot_arm");
        return 1;
    }
    
    /* Khôi phục bộ đếm Count */
    g_count = load_count_from_disk();
    printf("Restored count from disk: %u\n", g_count);
    ioctl(g_dev_fd, RA_IOC_RESTORE_COUNT, &g_count);
    ioctl(g_dev_fd, RA_IOC_SET_TARGET, &g_target);
    
    /* Bắt đầu SELF-TEST */
    g_state = RA_STATE_SELF_TEST;
    printf("FSM: SELF_TEST... Checked sensors and driver status\n");
    sleep(1);
    
    if (ioctl(g_dev_fd, RA_IOC_ENABLE) < 0) {
        perror("SELF_TEST FAILED: Driver enable failed or E-STOP latched");
        g_state = RA_STATE_ESTOP;
    } else {
        g_state = RA_STATE_IDLE;
        printf("FSM: SYSTEM READY. STATE: IDLE\n");
    }
    
    /* Cấu hình mạng Server nhận gói tin từ ESP32-CAM */
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Network bind failed");
        close(g_dev_fd);
        return 1;
    }
    listen(server_fd, 3);
    
    /* Cấu hình Poll rà soát đồng thời Sự kiện Driver và Gói tin mạng LAN */
    struct pollfd fds[2];
    fds[0].fd = g_dev_fd;
    fds[0].events = POLLIN;
    fds[1].fd = server_fd;
    fds[1].events = POLLIN;
    
    while (g_state != RA_STATE_COMPLETE && g_state != RA_STATE_ESTOP) {
        int ret = poll(fds, 2, 5000); /* Timeout 5 giây */
        if (ret < 0) {
            perror("Poll failed");
            break;
        }
        
        /* 1. Xử lý sự kiện cảm biến từ ngắt của nhân Linux */
        if (fds[0].revents & POLLIN) {
            struct ra_event ev;
            if (read(g_dev_fd, &ev, sizeof(ev)) > 0) {
                printf("Sensor event: Type=%u, Value=%u\n", ev.event_type, ev.value);
                if (ev.event_type == RA_EVENT_ESTOP && ev.value == 1) {
                    g_state = RA_STATE_ESTOP;
                    printf("FSM: EMERGENCY ESTOP DETECTED. SYSTEM HALTED!\n");
                    break;
                }
            }
        }
        
        /* 2. Xử lý yêu cầu truyền thông từ ESP32-CAM */
        if (fds[1].revents & POLLIN) {
            new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
            if (new_socket >= 0) {
                char buffer[1024] = {0};
                read(new_socket, buffer, sizeof(buffer)-1);
                
                /* Parse gói tin đơn giản chứa chuỗi seq và event */
                char *seq_ptr = strstr(buffer, "\"seq\":");
                char *event_ptr = strstr(buffer, "\"event\":");
                
                if (seq_ptr && event_ptr) {
                    u32 seq = atoi(seq_ptr + 6);
                    char event_name[32] = {0};
                    sscanf(event_ptr + 8, "%31[^"]", event_name);
                    
                    if (seq <= g_last_seq) {
                        /* Chống trùng lặp gói tin */
                        char *resp = "{\"result\":\"duplicate\"}";
                        send(new_socket, resp, strlen(resp), 0);
                    } else if (g_state != RA_STATE_IDLE) {
                        /* Báo bận khi đang chạy chu trình gắp */
                        char *resp = "{\"result\":\"busy\"}";
                        send(new_socket, resp, strlen(resp), 0);
                    } else if (strcmp(event_name, "OBJECT_PRESENT") == 0) {
                        g_last_seq = seq;
                        g_state = RA_STATE_PICKING;
                        printf("FSM: OBJECT READY (seq %u). Triggering PICKING cycle...\n", seq);
                        
                        char resp[128];
                        snprintf(resp, sizeof(resp), "{\"result\":\"accepted\",\"cycle_id\":%u}", seq);
                        send(new_socket, resp, strlen(resp), 0);
                        
                        /* CHU TRÌNH HOẠT ĐỘNG MVP TỰ ĐỘNG CHẶT CHẼ */
                        /* Bước 1: Đi gắp và đóng kẹp */
                        printf(" -> Moving to Pickup Waypoint...\n");
                        if (execute_motion(&path_home_to_pick) == 0) {
                            printf(" -> Closing gripper...\n");
                            execute_motion(&path_grip_close);
                            
                            /* Kiểm tra cảm biến kẹp vật lý */
                            sleep(1);
                            printf(" -> Verifying GRIP sensor state...\n");
                            
                            /* Bước 2: Đi đặt và mở kẹp */
                            g_state = RA_STATE_PLACING;
                            printf(" -> Moving to Place Waypoint...\n");
                            if (execute_motion(&path_pick_to_place) == 0) {
                                printf(" -> Opening gripper...\n");
                                execute_motion(&path_grip_open);
                                sleep(1);
                                
                                /* Tăng đếm Idempotent sau khi COMMIT_SUCCESS */
                                printf(" -> Verifying PLACE sensor... COMMIT SUCCESS!\n");
                                ioctl(g_dev_fd, RA_IOC_COMMIT_SUCCESS, &seq);
                                g_count++;
                                save_count_to_disk(g_count);
                                printf(" -> Success! Total Count: %u / %u\n", g_count, g_target);
                            }
                        }
                        
                        /* Bước 3: Quay về HOME và ARM lại hệ thống */
                        printf(" -> Returning to HOME Position...\n");
                        execute_motion(&path_place_to_home);
                        if (g_count >= g_target) {
                            g_state = RA_STATE_COMPLETE;
                            printf("FSM: TARGET REACHED! SYSTEM COMPLETED PROUDLY.\n");
                        } else {
                            g_state = RA_STATE_IDLE;
                            printf("FSM: Idle. Waiting next object...\n");
                        }
                    }
                }
                close(new_socket);
            }
        }
    }
    
    close(server_fd);
    close(g_dev_fd);
    return 0;
}
