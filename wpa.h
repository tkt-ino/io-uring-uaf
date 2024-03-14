#ifndef WPA_H
#define WPA_H

/**
 * wpa_supplicant の操作に関するヘッダファイル
*/

#include <stdint.h>
#include <sys/mman.h>
#include <stddef.h>

/**
 * wpa_supplicant のプロセスIDを取得する関数
 * @return wpa_supplicant のプロセスID
*/
int get_process_id();

/**
 * wpa_supplicant を起動する関数
 * @return 成功：0, 失敗：1
*/
int start_wpa_supplicant();

/**
 * wpa_supplicant を停止させる関数
 * @return 成功：0, 失敗：1
*/
int kill_wpa_supplicant();

/**
 * [heap] 領域が始まるアドレスを取得する関数
 * @param pid wpa_supplicant のプロセスID
 * @return [heap] 領域の先頭アドレス
*/
uint64_t get_heap_start_address(int pid);

/**
 * busy loop
 * @param time seconds
*/
void busy_loop(int time);

/**
 * 仮想アドレスを物理アドレスに変換する関数
 * @param pid プロセスID `0`の時は自身のプロセスIDとなる
 * @param virt_addr 仮想アドレス
 * @return 物理アドレス
*/
uint64_t v2p(int pid, void *virt_addr);

/**
 * 内部的に`mmap`を呼ぶ
*/
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

/**
 * 仮想アドレスと対応する物理アドレスの構造体
*/
typedef struct {
    void *virt_addr;
    uint64_t phys_addr;
} address;

#endif // WPA_H