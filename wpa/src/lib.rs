use std::{ffi::c_void, fs::File, io::{Read, Seek, SeekFrom}, mem::size_of, process::{Command, Stdio}, time};
use pagemap::maps;
use libc::{sysconf, _SC_PAGESIZE};

const WPA_SUPPLICANT_PATH: &str = "/home/inoue/wpa_supplicant-2.10/wpa_supplicant/wpa_supplicant";
const CONFIG_PATH: &str = "-c/home/inoue/wpa_supplicant-2.10/wpa_supplicant/wpa_supplicant.conf";
const INTERFACE: &str = "-iwlx00c0ca991249";

#[no_mangle]
pub extern "C" fn get_process_id() -> i32 {
    let ps = Command::new("ps").arg("aux").stdout(Stdio::piped()).spawn().expect("ps failed");
    let grep = Command::new("grep").arg("wpa_supplicant").stdin(ps.stdout.unwrap()).output().expect("grep failed");
    let mut pid = 0;
    for line in String::from_utf8(grep.stdout).unwrap().lines() {
        if !line.contains("grep") {
            let res: Vec<&str> = line.split_whitespace().collect();
            pid = res[1].parse::<i32>().unwrap();
        }
    }
    pid
}

#[no_mangle]
pub extern "C" fn start_wpa_supplicant() -> i32 {
    let cmd = Command::new(WPA_SUPPLICANT_PATH).arg(INTERFACE).arg(CONFIG_PATH).arg("-B").stdout(Stdio::null()).status().expect("failed to start wpa_supplicant");
    match cmd.success() {
        true => 0,
        false => 1,
    }
}

#[no_mangle]
pub extern "C" fn kill_wpa_supplicant() -> i32 {
    let cmd = Command::new("killall").arg("wpa_supplicant").stderr(Stdio::null()).status().expect("killall failed");
    match cmd.success() {
        true => 0,
        false => 1,
    }
}

#[no_mangle]
pub extern "C" fn get_heap_start_address(pid: i32) -> u64 {
    let map = maps(pid as u64);
    let mut res = 0;
    match map {
        Ok(map) => {
            for m in map {
                if m.path() == Some("[heap]") {
                    res = m.memory_region().start_address();
                    break;
                }
            }
            res
        },
        Err(_) => res,
    }
}

#[no_mangle]
pub extern "C" fn busy_loop(time: i32) {
    let now = time::Instant::now();
    loop {
        if now.elapsed().as_secs() > time as u64 { break; }
    }
}

#[no_mangle]
pub extern "C" fn v2p(pid: i32, virt_addr: *mut c_void) -> u64 {
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

    let page_map_path = match pid {
       0 => format!("/proc/self/pagemap"),
       pid => format!("/proc/{}/pagemap", pid),
    };
    let mut file = File::open(page_map_path).unwrap();
    
    // 仮想アドレスページ番号の分だけカーソルを進める
    file.seek(SeekFrom::Start(size_of::<u64>() as u64 * virt_pfn)).unwrap();

    // 8x8=64bit 取得
    file.read_exact(&mut buff).unwrap();

    // 物理ページ番号を計算
    let mut pfn = 0;
    for (index, val) in buff.iter().enumerate() {
        pfn += (*val as u64) << (index * 8); 
    }

    match pfn & 0x7fffffffffffff {
        0 => 0,
        p => p * page_size + offset, 
    }
}
