//! Ordinary Windows loader. No driver, manual mapping or anti-cheat bypass.
use anyhow::{Context, Result, bail, ensure};
use std::{ffi::c_void, fs, mem::size_of, os::windows::ffi::OsStrExt, path::Path, ptr};
use windows_sys::Win32::{
    Foundation::*,
    System::{
        Diagnostics::{Debug::WriteProcessMemory, ToolHelp::*},
        LibraryLoader::*,
        Memory::*,
        Threading::*,
    },
};

#[derive(Debug)]
pub struct LoaderPending;
impl std::fmt::Display for LoaderPending {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "Загрузчик не завершился за 15 секунд. Повторная попытка доступна после перезапуска игры."
        )
    }
}
impl std::error::Error for LoaderPending {}

struct Handle(HANDLE);
impl Handle {
    fn new(handle: HANDLE) -> Result<Self> {
        ensure!(
            !handle.is_null() && handle != INVALID_HANDLE_VALUE,
            "{}",
            std::io::Error::last_os_error()
        );
        Ok(Self(handle))
    }
}
impl Drop for Handle {
    fn drop(&mut self) {
        unsafe {
            CloseHandle(self.0);
        }
    }
}

fn wide(value: &std::ffi::OsStr) -> Vec<u16> {
    value.encode_wide().chain(Some(0)).collect()
}
fn string(value: &[u16]) -> String {
    String::from_utf16_lossy(&value[..value.iter().position(|&c| c == 0).unwrap_or(value.len())])
}

pub fn dota_processes() -> Result<Vec<u32>> {
    unsafe {
        let snapshot = Handle::new(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0))?;
        let mut entry: PROCESSENTRY32W = std::mem::zeroed();
        entry.dwSize = size_of::<PROCESSENTRY32W>() as u32;
        let mut found = Vec::new();
        let mut ok = Process32FirstW(snapshot.0, &mut entry);
        while ok != 0 {
            if string(&entry.szExeFile).eq_ignore_ascii_case("dota2.exe") {
                found.push(entry.th32ProcessID);
            }
            ok = Process32NextW(snapshot.0, &mut entry);
        }
        Ok(found)
    }
}

pub fn launch_dota() -> Result<()> {
    use windows_sys::Win32::UI::{Shell::ShellExecuteW, WindowsAndMessaging::SW_SHOWNORMAL};
    // Delegate launch and authentication to the user's installed Steam client.
    let operation = wide(std::ffi::OsStr::new("open"));
    let uri = wide(std::ffi::OsStr::new("steam://rungameid/570"));
    let result = unsafe {
        ShellExecuteW(
            ptr::null_mut(),
            operation.as_ptr(),
            uri.as_ptr(),
            ptr::null(),
            ptr::null(),
            SW_SHOWNORMAL,
        )
    } as isize;
    ensure!(
        result > 32,
        "Steam не открыл Dota 2 (код {result}). Проверьте установку Steam и вход в аккаунт."
    );
    Ok(())
}

fn modules(pid: u32) -> Result<Vec<MODULEENTRY32W>> {
    unsafe {
        // The loader can briefly mutate the module list; retry ERROR_BAD_LENGTH.
        let mut snapshot = None;
        for _ in 0..8 {
            let raw = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
            if raw != INVALID_HANDLE_VALUE {
                snapshot = Some(Handle::new(raw)?);
                break;
            }
            if GetLastError() != ERROR_BAD_LENGTH {
                break;
            }
        }
        let snapshot = snapshot.context("Не удалось прочитать модули процесса")?;
        let mut entry: MODULEENTRY32W = std::mem::zeroed();
        entry.dwSize = size_of::<MODULEENTRY32W>() as u32;
        ensure!(
            Module32FirstW(snapshot.0, &mut entry) != 0,
            "{}",
            std::io::Error::last_os_error()
        );
        let mut result = vec![entry];
        while Module32NextW(snapshot.0, &mut entry) != 0 {
            result.push(entry);
        }
        Ok(result)
    }
}

pub fn loaded(pid: u32, path: &Path) -> Result<bool> {
    let path = normalized(path);
    Ok(modules(pid)?
        .iter()
        .any(|m| normalized(Path::new(&string(&m.szExePath))) == path))
}
fn normalized(path: &Path) -> String {
    // Toolhelp returns canonical names; TEMP may contain an 8.3 user directory.
    let path = fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
    path.to_string_lossy()
        .trim_start_matches(r"\\?\")
        .replace('/', "\\")
        .to_lowercase()
}

pub fn missing_game_modules(pid: u32) -> Result<Vec<&'static str>> {
    let modules = modules(pid)?;
    Ok([
        "client.dll",
        "engine2.dll",
        "panorama.dll",
        "v8.dll",
        "schemasystem.dll",
        "resourcesystem.dll",
        "steam_api64.dll",
    ]
    .into_iter()
    .filter(|name| {
        !modules
            .iter()
            .any(|m| string(&m.szModule).eq_ignore_ascii_case(name))
    })
    .collect())
}

pub fn conflicting_module(pid: u32, path: &Path) -> Result<Option<String>> {
    let name = path.file_name().context("Нет имени DLL")?.to_string_lossy();
    Ok(modules(pid)?.iter().find_map(|module| {
        let module_name = string(&module.szModule);
        let module_path = string(&module.szExePath);
        ((module_name.eq_ignore_ascii_case(&name)
            || module_name.eq_ignore_ascii_case("ovr_skins.dll"))
            && normalized(Path::new(&module_path)) != normalized(path))
        .then_some(module_path)
    }))
}

pub fn validate_dll(path: &Path) -> Result<()> {
    let bytes = fs::read(path).context("Не удалось прочитать DLL")?;
    validate_pe(&bytes)
}
fn validate_pe(b: &[u8]) -> Result<()> {
    ensure!(
        b.len() >= 64 && &b[..2] == b"MZ",
        "Файл не является Windows DLL"
    );
    let offset = u32::from_le_bytes(b[60..64].try_into()?) as usize;
    let h = b
        .get(offset..offset.checked_add(24).context("Повреждённый PE")?)
        .context("Повреждённый PE")?;
    ensure!(&h[..4] == b"PE\0\0", "Повреждённая PE-сигнатура");
    ensure!(
        u16::from_le_bytes([h[4], h[5]]) == 0x8664,
        "Нужна DLL для Windows x64"
    );
    ensure!(
        u16::from_le_bytes([h[22], h[23]]) & 0x2000 != 0,
        "Выбран EXE вместо DLL"
    );
    Ok(())
}

pub fn inject(pid: u32, path: &Path) -> Result<()> {
    ensure!(dota_processes()?.contains(&pid), "Процесс Dota 2 завершён");
    let missing = missing_game_modules(pid)?;
    ensure!(
        missing.is_empty(),
        "Игра ещё запускается. Ожидаются: {}",
        missing.join(", ")
    );
    load_library_remote(pid, path)
}

fn load_library_remote(pid: u32, path: &Path) -> Result<()> {
    let path = fs::canonicalize(path).context("Не найден файл DLL")?;
    let path = path.as_path();
    validate_dll(path)?;
    if let Some(existing) = conflicting_module(pid, path)? {
        bail!("В игре уже загружена другая копия модуля: {existing}. Перезапустите игру.");
    }
    ensure!(
        !loaded(pid, path)?,
        "DLL уже загружена; для перезагрузки перезапустите игру"
    );
    unsafe {
        let process = Handle::new(OpenProcess(
            PROCESS_CREATE_THREAD
                | PROCESS_QUERY_INFORMATION
                | PROCESS_VM_OPERATION
                | PROCESS_VM_WRITE
                | PROCESS_VM_READ,
            0,
            pid,
        ))
        .context("Нет доступа к Dota 2")?;
        let mut architecture = 0u16;
        let mut native_architecture = 0u16;
        ensure!(
            IsWow64Process2(process.0, &mut architecture, &mut native_architecture) != 0
                && architecture == 0
                && native_architecture == 0x8664,
            "Нужен процесс Dota 2 x64"
        );
        let kernel = GetModuleHandleW(wide(std::ffi::OsStr::new("kernel32.dll")).as_ptr());
        let load = GetProcAddress(kernel, c"LoadLibraryW".as_ptr().cast())
            .context("LoadLibraryW не найдена")?;
        // Resolve the actual owning module, including forwarded kernel32 exports.
        let mut owner = ptr::null_mut();
        ensure!(
            GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                load as *const u16,
                &mut owner
            ) != 0,
            "Не найден модуль загрузчика"
        );
        let mut owner_path = [0u16; 32768];
        ensure!(
            GetModuleFileNameW(owner, owner_path.as_mut_ptr(), owner_path.len() as u32) > 0,
            "Не найден путь загрузчика"
        );
        let owner_name = Path::new(&string(&owner_path))
            .file_name()
            .context("Нет имени модуля")?
            .to_string_lossy()
            .into_owned();
        let remote_owner = modules(pid)?
            .into_iter()
            .find(|m| string(&m.szModule).eq_ignore_ascii_case(&owner_name))
            .context("Модуль загрузчика отсутствует в Dota 2")?;
        let address = remote_owner.modBaseAddr as usize + (load as usize - owner as usize);
        let path = wide(path.as_os_str());
        let size = path.len() * 2;
        let memory = VirtualAllocEx(
            process.0,
            ptr::null(),
            size,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE,
        );
        ensure!(
            !memory.is_null(),
            "VirtualAllocEx: {}",
            std::io::Error::last_os_error()
        );
        let mut written = 0;
        if WriteProcessMemory(process.0, memory, path.as_ptr().cast(), size, &mut written) == 0
            || written != size
        {
            VirtualFreeEx(process.0, memory, 0, MEM_RELEASE);
            bail!(
                "Не удалось передать путь DLL: {}",
                std::io::Error::last_os_error()
            );
        }
        let start: unsafe extern "system" fn(*mut c_void) -> u32 = std::mem::transmute(address);
        let thread = match Handle::new(CreateRemoteThread(
            process.0,
            ptr::null(),
            0,
            Some(start),
            memory,
            0,
            ptr::null_mut(),
        )) {
            Ok(thread) => thread,
            Err(error) => {
                VirtualFreeEx(process.0, memory, 0, MEM_RELEASE);
                return Err(error);
            }
        };
        let wait = WaitForSingleObject(thread.0, 15000);
        if wait != WAIT_OBJECT_0 {
            // The thread may still use the allocation. Never free live remote memory.
            return Err(LoaderPending.into());
        }
        VirtualFreeEx(process.0, memory, 0, MEM_RELEASE);
        ensure!(
            loaded(
                pid,
                Path::new(&String::from_utf16_lossy(&path[..path.len() - 1]))
            )?,
            "Windows не загрузила DLL. Проверьте зависимости и журнал."
        );
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_exe_x86_and_truncated_pe() {
        let mut b = vec![0; 128];
        b[..2].copy_from_slice(b"MZ");
        b[60] = 64;
        b[64..68].copy_from_slice(b"PE\0\0");
        b[68..70].copy_from_slice(&0x8664u16.to_le_bytes());
        b[86..88].copy_from_slice(&0x2000u16.to_le_bytes());
        assert!(validate_pe(&b).is_ok());
        b[86] = 0;
        b[87] = 0;
        assert!(validate_pe(&b).is_err());
        b[68..70].copy_from_slice(&0x14cu16.to_le_bytes());
        assert!(validate_pe(&b).is_err());
        b[60..64].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(validate_pe(&b).is_err());
        assert!(validate_pe(b"MZ").is_err());
    }

    #[test]
    #[ignore = "Used only as a child host by the loader integration test"]
    fn loader_child_process() {
        std::thread::sleep(std::time::Duration::from_secs(40));
    }

    #[test]
    fn loads_fixture_into_own_child_and_rejects_duplicate() -> Result<()> {
        let Some(fixture) = std::env::var_os("OVR_TEST_DLL") else {
            eprintln!("OVR_TEST_DLL unset: loader integration test skipped");
            return Ok(());
        };
        let directory = std::env::temp_dir().join(format!("ovr-тест-{}", std::process::id()));
        fs::create_dir_all(&directory)?;
        let path = directory.join("модуль.dll");
        fs::copy(fixture, &path)?;
        struct Child(std::process::Child);
        impl Drop for Child {
            fn drop(&mut self) {
                let _ = self.0.kill();
                let _ = self.0.wait();
            }
        }
        let child = Child(
            std::process::Command::new(std::env::current_exe()?)
                .args([
                    "--ignored",
                    "--exact",
                    "native::tests::loader_child_process",
                ])
                .spawn()?,
        );
        let pid = child.0.id();
        std::thread::sleep(std::time::Duration::from_millis(300));
        assert!(!loaded(pid, &path)?);
        load_library_remote(pid, &path)?;
        assert!(loaded(pid, &path)?);
        assert!(load_library_remote(pid, &path).is_err());
        let other = directory.join("other");
        fs::create_dir_all(&other)?;
        let other_dll = other.join("модуль.dll");
        fs::copy(&path, &other_dll)?;
        assert!(load_library_remote(pid, &other_dll).is_err());
        drop(child);
        fs::remove_file(other_dll)?;
        fs::remove_dir(other)?;
        fs::remove_file(path)?;
        fs::remove_dir(directory)?;
        Ok(())
    }
}
