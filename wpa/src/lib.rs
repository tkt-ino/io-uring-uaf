use std::{ffi::c_void, fs::File, io::{Read, Seek, SeekFrom}, mem::size_of, process::{Command, Stdio}, time};
use pagemap::maps;
use libc::{sysconf, _SC_PAGESIZE};
mod config;
use config::{WPA_SUPPLICANT_PATH, CONFIG_PATH, INTERFACE};

#[no_mangle]
pub extern "C" fn get_process_id() -> i32 {
    let mut pid = 0;
    let ps = match Command::new("ps").arg("aux").stdout(Stdio::piped()).spawn() {
        Ok(ps) => match ps.stdout {
            Some(stdout) => stdout,
            None => return pid,
        }
        Err(_) => return pid,
    };
    let grep = match Command::new("grep").arg("wpa_supplicant").stdin(ps).output() {
        Ok(output) => output,
        Err(_) => return pid,
    };
    let lines = match String::from_utf8(grep.stdout) {
        Ok(lines) => lines,
        Err(_) => return pid,
    };
    for line in lines.lines() {
        if !line.contains("grep") {
            let res: Vec<&str> = line.split_whitespace().collect();
            pid = res[1].parse::<i32>().unwrap_or(0);
            break;
        }
    }
    pid
}

#[no_mangle]
pub extern "C" fn start_wpa_supplicant() -> i32 {
    match Command::new(WPA_SUPPLICANT_PATH).arg(INTERFACE).arg(CONFIG_PATH).arg("-B").stdout(Stdio::null()).status() {
        Err(_) => 1,
        Ok(status) => match status.success() {
            true => 0,
            false => 1,
        }
    }
}

#[no_mangle]
pub extern "C" fn kill_wpa_supplicant() -> i32 {
    match Command::new("killall").arg("wpa_supplicant").stderr(Stdio::null()).status() {
        Err(_) => 1,
        Ok(status) => match status.success() {
            true => 0,
            false => 1,
        }
    }
}

#[no_mangle]
pub extern "C" fn get_heap_start_address(pid: i32) -> u64 {
    let mut res = 0;
    let map = match maps(pid as u64) {
        Ok(map) => map,
        Err(_) => return res,
    };
    for m in map {
        if m.path() == Some("[heap]") {
            res = m.memory_region().start_address();
            break;
        }
    }
    res
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
       0 => "/proc/self/pagemap".to_string(),
       pid => format!("/proc/{}/pagemap", pid),
    };
    let mut file = match File::open(page_map_path) {
        Ok(file) => file,
        Err(_) => {
            println!("[-] failed to open pagemap file");
            return 0;
        }
    };
    if file.seek(SeekFrom::Start(size_of::<u64>() as u64 * virt_pfn)).is_err() {
        println!("[-] failed to seek pagemap file");
        return 0;
    }
    if file.read_exact(&mut buff).is_err() {
        println!("[-] failed to read pagemap file");
        return 0;
    }
    let mut pfn = 0;
    for (index, val) in buff.iter().enumerate() {
        pfn += (*val as u64) << (index * size_in_bits::<u8>());
    }
    match pfn & 0x7fffffffffffff {
        0 => 0,
        p => p * page_size + offset,
    }
}

fn size_in_bits<T>() -> usize {
    size_of::<T>() * 8
}
