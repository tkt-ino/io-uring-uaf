#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <sys/mman.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

void err_exit(const char *msg) {
    puts(msg);
    exit(-1);
}

/**
 * 内部的に`mmap`を呼ぶ
 * 確保するサイズは1ページ分(4KiB)
 * @return 確保した領域の先頭アドレス
*/
void *mmap_custom() {
    void *addr = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
    return addr;
}

/**
 * 内部的に`mmap`を呼ぶ
 * アドレスを指定可能
 * @return 確保した領域の先頭アドレス
*/
void *mmap_fixed(void *addr) {
    void *new_mem = mmap(addr, 0x1000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    if (new_mem == MAP_FAILED) err_exit("[-] mmap() failed");
    return new_mem;
}

/**
 * 仮想アドレスと対応する物理アドレスの構造体
*/
typedef struct {
    void *virt_addr;
    uint64_t phys_addr;
} address;

#endif // UTILS_H
