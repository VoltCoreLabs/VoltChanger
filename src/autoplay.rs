use std::time::{Duration, Instant};

pub struct AutoRun {
    deadline: Instant,
    observed_pid: Option<u32>,
}
#[derive(Debug, PartialEq, Eq)]
pub enum Action {
    Wait,
    Load(u32),
    Finish,
    Stop(&'static str),
}

impl AutoRun {
    pub fn new(now: Instant) -> Self {
        Self {
            deadline: now + Duration::from_secs(180),
            observed_pid: None,
        }
    }
    pub fn poll(&mut self, now: Instant, s: &crate::state::Snapshot) -> Action {
        if !s.valid {
            return Action::Stop("Автозапуск остановлен: DLL недоступна.");
        }
        if s.uncertain_pid.is_some() {
            return Action::Stop("Автозапуск остановлен: предыдущая загрузка ещё может работать.");
        }
        if s.conflicting {
            return Action::Stop(
                "Автозапуск остановлен: в игре уже находится другая копия DLL. Перезапустите игру.",
            );
        }
        if s.loaded {
            return Action::Finish;
        }
        if now >= self.deadline {
            return Action::Stop(
                "За 3 минуты игра не подготовилась. Проверьте окно Steam и журнал, затем повторите запуск.",
            );
        }
        if let Some(previous) = self.observed_pid
            && s.pid != Some(previous)
        {
            return Action::Stop("Игра завершилась или процесс изменился. Автозагрузка отменена.");
        }
        if let Some(pid) = s.pid {
            self.observed_pid = Some(pid);
            if s.game_ready {
                return Action::Load(pid);
            }
        }
        Action::Wait
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::state::{Initialization, Snapshot};
    fn snapshot() -> Snapshot {
        Snapshot {
            dll: Default::default(),
            pid: None,
            loaded: false,
            valid: true,
            game_ready: false,
            plus_enabled: true,
            recommended_builds: false,
            conflicting: false,
            auto_running: true,
            initialization: Initialization::Unknown,
            busy: false,
            uncertain_pid: None,
            status: String::new(),
            log: Default::default(),
            dll_log: String::new(),
        }
    }
    #[test]
    fn waits_for_process_and_modules_then_loads() {
        let now = Instant::now();
        let mut run = AutoRun::new(now);
        let mut s = snapshot();
        assert_eq!(run.poll(now, &s), Action::Wait);
        s.pid = Some(7);
        assert_eq!(run.poll(now, &s), Action::Wait);
        s.game_ready = true;
        assert_eq!(run.poll(now, &s), Action::Load(7));
    }
    #[test]
    fn stops_on_exit_timeout_and_conflicting_dll() {
        let now = Instant::now();
        let mut run = AutoRun::new(now);
        let mut s = snapshot();
        s.pid = Some(7);
        assert_eq!(run.poll(now, &s), Action::Wait);
        s.pid = None;
        assert!(matches!(run.poll(now, &s), Action::Stop(_)));
        let mut run = AutoRun::new(now);
        assert!(matches!(
            run.poll(now + Duration::from_secs(181), &s),
            Action::Stop(_)
        ));
        s.conflicting = true;
        assert!(matches!(run.poll(now, &s), Action::Stop(_)));
    }
    #[test]
    fn never_loads_missing_dll_or_repeats_an_existing_load() {
        let now = Instant::now();
        let mut run = AutoRun::new(now);
        let mut s = snapshot();
        s.pid = Some(7);
        s.game_ready = true;
        s.valid = false;
        assert!(matches!(run.poll(now, &s), Action::Stop(_)));
        s.valid = true;
        s.loaded = true;
        assert_eq!(run.poll(now, &s), Action::Finish);
        s.loaded = false;
        s.uncertain_pid = Some(7);
        assert!(matches!(run.poll(now, &s), Action::Stop(_)));
    }
}
