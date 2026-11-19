use crate::{
    autoplay::{Action, AutoRun},
    native,
};
use serde::{Deserialize, Serialize};
use std::{
    collections::VecDeque,
    fs,
    path::PathBuf,
    sync::mpsc::{self, Receiver, Sender},
    time::{Duration, Instant},
};

#[derive(Clone, Default, Serialize, Deserialize)]
struct Settings {
    dll: PathBuf,
}
fn settings_path() -> PathBuf {
    std::env::var_os("LOCALAPPDATA")
        .map(PathBuf::from)
        .unwrap_or_else(std::env::temp_dir)
        .join("OVR/launcher.json")
}
fn recommended_builds_path() -> PathBuf {
    std::env::var_os("LOCALAPPDATA")
        .map(PathBuf::from)
        .unwrap_or_else(std::env::temp_dir)
        .join("OVR/recommended_builds.cfg")
}
fn default_dll() -> PathBuf {
    let exe = std::env::current_exe().unwrap_or_default();
    let beside = exe.with_file_name("ovr_skins.dll");
    if !beside.exists()
        && let Some(target) = exe.parent().and_then(|profile| profile.parent())
        && target.file_name().is_some_and(|name| name == "target")
        && let Some(project) = target.parent()
    {
        let packaged = std::fs::read_to_string(project.join("build/native/ovr-dll-path.txt"))
            .map(|path| PathBuf::from(path.trim()))
            .unwrap_or_else(|_| project.join("build/native/package/Release/ovr_skins.dll"));
        return if packaged.exists() {
            packaged
        } else {
            project.join("build/native/Release/ovr_skins.dll")
        };
    }
    beside
}

#[derive(Clone)]
pub struct Snapshot {
    pub dll: PathBuf,
    pub pid: Option<u32>,
    pub loaded: bool,
    pub valid: bool,
    pub game_ready: bool,
    pub plus_enabled: bool,
    pub recommended_builds: bool,
    pub conflicting: bool,
    pub auto_running: bool,
    pub initialization: Initialization,
    pub busy: bool,
    pub uncertain_pid: Option<u32>,
    pub status: String,
    pub log: VecDeque<String>,
    pub dll_log: String,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Initialization {
    Unknown,
    Waiting,
    Ready,
    Failed,
}

fn initialization_from_log(log: &str, pid: u32) -> Initialization {
    if let Some(session) = log
        .lines()
        .find_map(|line| line.strip_prefix("[+] OVR session pid="))
        && session.trim().parse::<u32>().ok() != Some(pid)
    {
        return Initialization::Unknown;
    }
    for line in log.lines().rev() {
        if line.starts_with("[-]")
            && (line.contains("init failed") || line.contains("OVR initialization"))
        {
            return Initialization::Failed;
        }
        if line == "[+] OVR initialization complete" {
            return Initialization::Ready;
        }
        if line.starts_with("[+] OVR waiting for ") {
            return Initialization::Waiting;
        }
    }
    Initialization::Unknown
}
pub enum Command {
    TogglePlus,
    ToggleRecommendedBuilds,
    Start,
    Cancel,
    Refresh,
    Pick,
    Load,
    Folder,
}
impl Snapshot {
    fn note(&mut self, text: impl Into<String>) {
        use std::io::Write;
        let text = text.into();
        let file = settings_path().with_file_name("launcher.log");
        if let Some(parent) = file.parent() {
            let _ = fs::create_dir_all(parent);
        }
        if let Ok(mut file) = fs::OpenOptions::new().create(true).append(true).open(file) {
            let _ = writeln!(file, "[launcher {}] {text}", std::process::id());
        }
        self.log.push_back(text);
        while self.log.len() > 60 {
            self.log.pop_front();
        }
    }
    fn refresh(&mut self) {
        self.recommended_builds = fs::read_to_string(recommended_builds_path())
            .map(|s| s.lines().any(|line| line.trim() == "enabled=1"))
            .unwrap_or(false);
        self.plus_enabled = fs::read_to_string(self.dll.with_file_name("ovr_plus.cfg"))
            .map(|s| !s.lines().any(|line| line.trim() == "enabled=0"))
            .unwrap_or(true);
        self.game_ready = false;
        self.conflicting = false;
        self.initialization = Initialization::Unknown;
        let dll_error = native::validate_dll(&self.dll).err();
        self.valid = dll_error.is_none();
        match native::dota_processes() {
            Ok(pids) => {
                if self
                    .uncertain_pid
                    .is_some_and(|pending| !pids.contains(&pending))
                {
                    self.uncertain_pid = None;
                }
                self.pid = if pids.len() == 1 { Some(pids[0]) } else { None };
                self.loaded = false;
                self.status = if pids.len() > 1 {
                    "Найдено несколько Dota 2. Оставьте один процесс.".into()
                } else if let Some(pid) = self.pid {
                    match native::loaded(pid, &self.dll) {
                        Ok(loaded) => {
                            self.loaded = loaded;
                            if loaded {
                                "DLL загружена. Инициализация — в журнале модуля.".into()
                            } else {
                                match native::missing_game_modules(pid) {
                                    Ok(missing) if missing.is_empty() => {
                                        match native::conflicting_module(pid, &self.dll) {
                                            Ok(Some(path)) => {
                                                self.conflicting = true;
                                                format!(
                                                    "В игре уже загружена другая копия: {path}. Перезапустите Dota 2."
                                                )
                                            }
                                            Ok(None) => {
                                                self.game_ready = true;
                                                "Модули игры загружены. Дождитесь главного меню и загрузите DLL.".into()
                                            }
                                            Err(e) => format!(
                                                "Не удалось проверить загруженные DLL: {e:#}"
                                            ),
                                        }
                                    }
                                    Ok(missing) => format!(
                                        "Игра запускается. Ожидаются: {}",
                                        missing.join(", ")
                                    ),
                                    Err(e) => format!("Не удалось проверить модули игры: {e:#}"),
                                }
                            }
                        }
                        Err(e) => {
                            self.valid = false;
                            format!("Не удалось проверить процесс: {e:#}")
                        }
                    }
                } else {
                    "Запустите Dota 2 и дождитесь главного меню.".into()
                };
            }
            Err(e) => {
                self.pid = None;
                self.loaded = false;
                self.status = format!("Ошибка проверки процессов: {e:#}");
            }
        }
        if !self.valid && !self.dll.exists() {
            self.status = "DLL не найдена. Выберите файл или выполните build.ps1.".into();
        } else if let Some(error) = dll_error {
            self.status = format!("DLL недоступна: {error:#}");
        }
        if self.uncertain_pid.is_some() {
            self.status =
                "Загрузчик ещё может работать. Для новой попытки перезапустите игру.".into();
        }
        let path = self.dll.with_file_name("ovr_skins.log");
        self.dll_log = read_tail(&path).unwrap_or_default();
        if self.loaded
            && let Some(pid) = self.pid
        {
            self.initialization = initialization_from_log(&self.dll_log, pid);
            self.status = match self.initialization {
                Initialization::Failed => "Инициализация DLL завершилась ошибкой. Причина ниже в журнале. Для новой попытки перезапустите игру.".into(),
                Initialization::Waiting => "DLL ожидает загрузки модулей игры…".into(),
                Initialization::Ready => "Инициализация SDK и хуков завершена. Проверьте предметы в инвентаре Dota 2.".into(),
                Initialization::Unknown => "DLL загружена; успешная инициализация не подтверждена.".into(),
            };
        }
    }
}
fn read_tail(path: &std::path::Path) -> std::io::Result<String> {
    use std::io::{Read, Seek, SeekFrom};
    let mut file = fs::File::open(path)?;
    let size = file.metadata()?.len();
    file.seek(SeekFrom::Start(size.saturating_sub(24000)))?;
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes)?;
    Ok(String::from_utf8_lossy(&bytes).into_owned())
}
pub fn start() -> (Sender<Command>, Receiver<Snapshot>) {
    let (tx, commands) = mpsc::channel();
    let (updates, rx) = mpsc::channel();
    std::thread::spawn(move || {
        let settings = fs::read(settings_path())
            .ok()
            .and_then(|b| serde_json::from_slice::<Settings>(&b).ok())
            .unwrap_or_default();
        let mut s = Snapshot {
            dll: if settings.dll.as_os_str().is_empty() {
                default_dll()
            } else {
                settings.dll
            },
            pid: None,
            loaded: false,
            valid: false,
            game_ready: false,
            plus_enabled: true,
            recommended_builds: false,
            conflicting: false,
            auto_running: false,
            initialization: Initialization::Unknown,
            busy: false,
            uncertain_pid: None,
            status: String::new(),
            log: VecDeque::new(),
            dll_log: String::new(),
        };
        s.note(
            "VoltChanger запущен. «Запустить Dota 2» откроет игру через Steam и подключит модуль.",
        );
        let mut auto: Option<AutoRun> = None;
        loop {
            s.refresh();
            if let Some(run) = auto.as_mut() {
                match run.poll(Instant::now(), &s) {
                    Action::Wait => {
                        if s.pid.is_none() {
                            s.status = "Ожидаем Dota 2. Если Steam просит вход или обновление, завершите его в окне Steam.".into();
                        }
                    }
                    Action::Load(pid) => {
                        // Consume the request before loading: failures never cause automatic retries.
                        auto = None;
                        s.auto_running = false;
                        load(&mut s, pid, &updates);
                        s.refresh();
                    }
                    Action::Finish => {
                        auto = None;
                        s.auto_running = false;
                        s.note("DLL уже загружена. Повторная загрузка не требуется.");
                    }
                    Action::Stop(reason) => {
                        auto = None;
                        s.auto_running = false;
                        s.note(reason);
                    }
                }
            }
            if updates.send(s.clone()).is_err() {
                break;
            }
            match commands.recv_timeout(Duration::from_secs(2)) {
                Ok(Command::ToggleRecommendedBuilds) => {
                    let path = recommended_builds_path();
                    if let Some(parent) = path.parent() {
                        let _ = fs::create_dir_all(parent);
                    }
                    let text = if s.recommended_builds {
                        "enabled=0\n"
                    } else {
                        "enabled=1\n"
                    };
                    match fs::write(path, text) {
                        Ok(()) => s.note("Настройка рекомендованных сборок сохранена."),
                        Err(e) => s.note(format!("Не удалось сохранить настройку сборок: {e}")),
                    }
                }
                Ok(Command::TogglePlus) => {
                    if s.loaded || s.busy || s.auto_running {
                        continue;
                    }
                    let path = s.dll.with_file_name("ovr_plus.cfg");
                    let text = if s.plus_enabled {
                        "enabled=0\n"
                    } else {
                        "enabled=1\n"
                    };
                    match fs::write(path, text) {
                        Ok(()) => s.note(
                            "Настройка локальной Dota+ сохранена для следующей загрузки DLL.",
                        ),
                        Err(e) => s.note(format!("Не удалось сохранить настройку Dota+: {e}")),
                    }
                }
                Ok(Command::Start) => {
                    if auto.is_some() {
                        continue;
                    }
                    s.refresh();
                    if !s.valid || s.uncertain_pid.is_some() || s.conflicting {
                        s.note("Запуск недоступен: проверьте DLL и состояние игры.");
                        continue;
                    }
                    match native::dota_processes() {
                        Ok(pids) if pids.len() > 1 => {
                            s.note("Оставьте один процесс Dota 2 перед запуском.")
                        }
                        Ok(pids) => {
                            if pids.is_empty() {
                                if let Err(e) = native::launch_dota() {
                                    s.note(format!("Ошибка запуска игры: {e:#}"));
                                    continue;
                                }
                                s.note("Steam получил команду запуска Dota 2. Ожидаем игру и её модули…");
                            } else {
                                s.note("Dota 2 уже запущена. Ожидаем готовность к загрузке DLL…");
                            }
                            auto = Some(AutoRun::new(Instant::now()));
                            s.auto_running = true;
                        }
                        Err(e) => s.note(format!("Не удалось проверить Dota 2: {e:#}")),
                    }
                }
                Ok(Command::Cancel) => {
                    auto = None;
                    s.auto_running = false;
                    s.note("Автозагрузка отменена. Игра продолжает работать.");
                }
                Ok(Command::Pick) => {
                    if let Some(path) = rfd::FileDialog::new()
                        .add_filter("Windows DLL", &["dll"])
                        .pick_file()
                    {
                        match native::validate_dll(&path) {
                            Ok(()) => {
                                s.dll = path;
                                s.note(format!("Выбрана {}", s.dll.display()));
                                let file = settings_path();
                                let saved = (|| -> anyhow::Result<()> {
                                    fs::create_dir_all(file.parent().unwrap())?;
                                    fs::write(
                                        file,
                                        serde_json::to_vec_pretty(&Settings {
                                            dll: s.dll.clone(),
                                        })?,
                                    )?;
                                    Ok(())
                                })();
                                if let Err(e) = saved {
                                    s.note(format!("Путь не сохранён: {e:#}"));
                                }
                            }
                            Err(e) => s.note(format!("Файл отклонён: {e:#}")),
                        }
                    }
                }
                Ok(Command::Load) => {
                    if auto.is_some() {
                        continue;
                    }
                    s.refresh();
                    if let Some(pid) = s.pid.filter(|_| {
                        s.valid && s.game_ready && !s.loaded && s.uncertain_pid.is_none()
                    }) {
                        load(&mut s, pid, &updates);
                    } else {
                        s.note("Загрузка недоступна: проверьте процесс и DLL.");
                    }
                }
                Ok(Command::Folder) => {
                    if let Some(folder) = s.dll.parent()
                        && let Err(e) = std::process::Command::new("explorer.exe")
                            .arg(folder)
                            .spawn()
                    {
                        s.note(format!("Не удалось открыть папку: {e}"));
                    }
                }
                Ok(Command::Refresh) | Err(mpsc::RecvTimeoutError::Timeout) => {}
                Err(mpsc::RecvTimeoutError::Disconnected) => break,
            }
        }
    });
    if std::env::args().any(|arg| arg == "--play") {
        let _ = tx.send(Command::Start);
    }
    (tx, rx)
}

fn load(s: &mut Snapshot, pid: u32, updates: &Sender<Snapshot>) {
    s.busy = true;
    s.note(format!("Загрузка DLL в Dota 2, PID {pid}…"));
    if updates.send(s.clone()).is_err() {
        s.busy = false;
        return;
    }
    match native::inject(pid, &s.dll) {
        Ok(()) => s.note("Windows загрузила DLL. Ожидаем результат инициализации в журнале."),
        Err(e) => {
            if e.is::<native::LoaderPending>() {
                s.uncertain_pid = Some(pid);
            }
            s.note(format!("Ошибка загрузки: {e:#}"));
        }
    }
    s.busy = false;
}

#[cfg(test)]
mod tests {
    use super::{Initialization::*, initialization_from_log};
    #[test]
    fn distinguishes_loaded_waiting_failure_success_and_stale_session() {
        assert_eq!(
            initialization_from_log(
                "[+] Loaded\n[-] Module not loaded: panorama.dll\n[-] UIEngine init failed\n",
                9
            ),
            Failed
        );
        assert_eq!(initialization_from_log("[+] Loaded\n", 9), Unknown);
        assert_eq!(
            initialization_from_log(
                "[+] OVR session pid=9\n[+] OVR waiting for module: panorama.dll\n",
                9
            ),
            Waiting
        );
        assert_eq!(
            initialization_from_log(
                "[+] OVR session pid=9\n[+] OVR initialization complete\n",
                9
            ),
            Ready
        );
        assert_eq!(
            initialization_from_log(
                "[+] OVR session pid=8\n[+] OVR initialization complete\n",
                9
            ),
            Unknown
        );
        assert_eq!(
            initialization_from_log(
                "[-] OVR initialization incomplete: one or more hooks failed\n",
                9
            ),
            Failed
        );
    }
}
