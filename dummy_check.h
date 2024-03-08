#ifndef DUMMY_CHECK_H
#define DUMMY_CHECK_H

#include <stdint.h>

int get_process_id();
int kill_wpa_supplicant();
int start_wpa_supplicant();
uint64_t get_heap_start_address(int);
void busy_loop(int);
uint64_t v2p(int pid, void *virt_addr);

#endif // DUMMY_CHECK_H