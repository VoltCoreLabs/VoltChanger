use crate::state::{self, Command, Initialization, Snapshot};
use gpui_kit::component::switch::Switch;
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::{
    component::{button::*, *},
    *,
};
use std::{
    sync::mpsc::{Receiver, Sender},
    time::Duration,
};

const BG: u32 = 0x181c20;
const SIDE: u32 = 0x13171a;
const SURFACE: u32 = 0x232a2e;
const BORDER: u32 = 0x354047;
const TEXT: u32 = 0xeceee9;
const MUTED: u32 = 0x9aa9ad;
const GOLD: u32 = 0xddc08c;
const GREEN: u32 = 0x91bda5;
const RED: u32 = 0xe99c91;

#[derive(Clone, Copy, PartialEq, Eq)]
enum Page {
    Launch,
    Modules,
    Journal,
}
pub struct Launcher {
    tx: Sender<Command>,
    rx: Receiver<Snapshot>,
    snapshot: Option<Snapshot>,
    page: Page,
    native_log: bool,
}
impl Launcher {
    pub fn new(cx: &mut Context<Self>) -> Self {
        let (tx, rx) = state::start();
        cx.spawn(async |this, cx| {
            loop {
                cx.background_executor()
                    .timer(Duration::from_millis(150))
                    .await;
                if this
                    .update(cx, |this, cx| {
                        let mut changed = false;
                        while let Ok(s) = this.rx.try_recv() {
                            this.snapshot = Some(s);
                            changed = true;
                        }
                        if changed {
                            cx.notify();
                        }
                    })
                    .is_err()
                {
                    break;
                }
            }
        })
        .detach();
        Self {
            tx,
            rx,
            snapshot: None,
            page: Page::Launch,
            native_log: false,
        }
    }
    fn send(&self, command: Command) {
        let _ = self.tx.send(command);
    }
    fn nav(&self, page: Page, label: &'static str, cx: &mut Context<Self>) -> impl IntoElement {
        let selected = self.page == page;
        Button::new(label)
            .ghost()
            .label(label)
            .w_full()
            .h(px(42.))
            .justify_start()
            .bg(rgb(if selected { SURFACE } else { SIDE }))
            .text_color(rgb(if selected { GOLD } else { MUTED }))
            .on_click(cx.listener(move |this, _, _, cx| {
                this.page = page;
                cx.notify();
            }))
    }
}
fn tag(text: impl Into<SharedString>, color: u32) -> impl IntoElement {
    div()
        .flex()
        .items_center()
        .gap_2()
        .text_size(px(12.))
        .text_color(rgb(color))
        .child(div().size(px(6.)).rounded_full().bg(rgb(color)))
        .child(text.into())
}
fn heading(title: &'static str, detail: &'static str) -> impl IntoElement {
    div()
        .flex()
        .flex_col()
        .gap_2()
        .child(
            div()
                .font_family("Segoe UI Variable Display")
                .text_size(px(30.))
                .font_weight(FontWeight::SEMIBOLD)
                .child(title),
        )
        .child(div().text_color(rgb(MUTED)).child(detail))
}
fn feature(
    title: &'static str,
    detail: &'static str,
    state: &'static str,
    color: u32,
) -> impl IntoElement {
    div()
        .flex()
        .items_center()
        .justify_between()
        .gap_4()
        .py_4()
        .border_b_1()
        .border_color(rgb(BORDER))
        .child(
            div()
                .flex_1()
                .min_w_0()
                .flex()
                .flex_col()
                .gap_1()
                .child(div().font_weight(FontWeight::MEDIUM).child(title))
                .child(
                    div()
                        .text_size(px(12.))
                        .text_color(rgb(MUTED))
                        .child(detail),
                ),
        )
        .child(tag(state, color))
}
fn checkpoint(label: &'static str, value: String, ready: bool) -> impl IntoElement {
    div()
        .flex_1()
        .min_w_0()
        .flex()
        .flex_col()
        .gap_2()
        .child(div().text_size(px(12.)).text_color(rgb(MUTED)).child(label))
        .child(tag(value, if ready { GREEN } else { MUTED }))
}
impl Render for Launcher {
    fn render(&mut self, _: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let s = self.snapshot.as_ref();
        let busy = s.is_some_and(|s| s.busy);
        let waiting = s.is_some_and(|s| s.auto_running);
        let loaded = s.is_some_and(|s| s.loaded);
        let ready = s.is_some_and(|s| s.initialization == Initialization::Ready && s.loaded);
        let failed = s.is_some_and(|s| {
            s.conflicting
                || s.uncertain_pid.is_some()
                || s.initialization == Initialization::Failed
                || !s.valid
        });
        let plus = s.is_some_and(|s| s.plus_enabled);
        let recommended = s.is_some_and(|s| s.recommended_builds);
        let can_start = s.is_some_and(|s| {
            s.valid
                && !s.loaded
                && !s.conflicting
                && !s.busy
                && !s.auto_running
                && s.uncertain_pid.is_none()
        });
        let can_load = s.is_some_and(|s| {
            s.valid
                && s.game_ready
                && s.pid.is_some()
                && !s.loaded
                && !s.busy
                && !s.auto_running
                && s.uncertain_pid.is_none()
        });
        let status = s
            .map(|s| s.status.clone())
            .unwrap_or_else(|| "Проверяем игру и модуль…".into());
        let path = s.map(|s| s.dll.display().to_string()).unwrap_or_default();
        let title = if ready {
            "Можно играть"
        } else if busy {
            "Подключаем модуль"
        } else if waiting {
            "Готовим игру к запуску"
        } else if failed {
            "Нужно немного внимания"
        } else {
            "Твоя игра. Твой стиль."
        };
        let state_label = if ready {
            "Подключено"
        } else if busy || waiting {
            "Запуск"
        } else if failed {
            "Проверь настройки"
        } else {
            "Ожидает запуска"
        };
        let state_color = if ready {
            GREEN
        } else if failed {
            RED
        } else {
            GOLD
        };
        let action = if busy {
            "Подключаем модуль…"
        } else if waiting {
            "Ожидаем Dota 2…"
        } else if ready {
            "Модуль подключён"
        } else if loaded {
            "Модуль уже загружен"
        } else {
            "Запустить Dota 2"
        };
        let gold_button = ButtonCustomVariant::new(cx)
            .color(rgb(GOLD).into())
            .foreground(rgb(SIDE).into())
            .hover(rgb(0xead2a7).into())
            .active(rgb(0xc5a976).into());
        let sidebar = div()
            .w(px(184.))
            .h_full()
            .flex_shrink_0()
            .bg(rgb(SIDE))
            .border_r_1()
            .border_color(rgb(BORDER))
            .p_5()
            .flex()
            .flex_col()
            .child(
                div()
                    .pt_3()
                    .pb_8()
                    .flex()
                    .flex_col()
                    .gap_1()
                    .child(
                        div()
                            .font_family("Segoe UI Variable Display")
                            .text_size(px(22.))
                            .font_weight(FontWeight::BOLD)
                            .text_color(rgb(GOLD))
                            .child("VoltChanger"),
                    )
                    .child(
                        div()
                            .text_size(px(11.))
                            .text_color(rgb(MUTED))
                            .child("Dota 2 companion"),
                    ),
            )
            .child(
                div()
                    .flex()
                    .flex_col()
                    .gap_2()
                    .child(self.nav(Page::Launch, "Запуск", cx))
                    .child(self.nav(Page::Modules, "Возможности", cx))
                    .child(self.nav(Page::Journal, "Журнал", cx)),
            )
            .child(div().flex_1())
            .child(
                div()
                    .border_t_1()
                    .border_color(rgb(BORDER))
                    .pt_4()
                    .flex()
                    .flex_col()
                    .gap_2()
                    .child(tag("Rust + GPUI Kit", MUTED))
                    .child(
                        div()
                            .text_size(px(11.))
                            .text_color(rgb(MUTED))
                            .child(format!("Версия {}  /  x64", env!("CARGO_PKG_VERSION"))),
                    ),
            );
        let mut content = div()
            .id("page-content")
            .flex_1()
            .min_w_0()
            .h_full()
            .overflow_y_scroll()
            .p_8()
            .flex()
            .flex_col()
            .gap_6();
        match self.page {
            Page::Launch => {
                content = content
                    .child(div().flex().items_center().justify_between()
                        .child(div().text_color(rgb(MUTED)).child("Главная / Dota 2")).child(tag(state_label, state_color)))
                    .child(div().bg(rgb(SURFACE)).rounded_xl().border_1().border_color(rgb(BORDER)).p_6().flex().flex_col().gap_5()
                        .child(heading(title, "Предметы, статистика и помощник внутри Dota."))
                        .child(div().text_color(rgb(MUTED)).min_h(px(42.)).child(status.clone()))
                        .child(div().flex().items_center().gap_3()
                            .child(Button::new("start").custom(gold_button).label(action).h(px(48.)).px_6().disabled(!can_start)
                                .on_click(cx.listener(|this, _, _, _| this.send(Command::Start))))
                            .when(waiting, |el| el.child(Button::new("cancel").ghost().label("Отменить ожидание").disabled(busy).on_click(cx.listener(|this, _, _, _| this.send(Command::Cancel)))))
                            .when(!waiting && !loaded, |el| el.child(Button::new("load").ghost().label("Подключить к открытой игре").disabled(!can_load).on_click(cx.listener(|this, _, _, _| this.send(Command::Load))))))
                        .child(div().border_t_1().border_color(rgb(BORDER)).pt_5().flex().gap_5()
                            .child(checkpoint("Файл модуля", if s.is_some_and(|s| s.valid) { "Готов" } else { "Проверка файла" }.into(), s.is_some_and(|s| s.valid)))
                            .child(checkpoint("Dota 2", s.and_then(|s| s.pid).map(|p| format!("Процесс {p}")).unwrap_or_else(|| "Не запущена".into()), s.is_some_and(|s| s.game_ready)))
                            .child(checkpoint("Подключение", if ready { "Готово" } else if loaded { "Инициализация" } else { "Ожидание" }.into(), ready))))
                    .child(div().flex().flex_col()
                        .child(div().font_weight(FontWeight::SEMIBOLD).child("Внутри игры"))
                        .child(feature("Твой инвентарь", "Предметы, экипировка и стили сохраняются между запусками.", "В клиенте", GOLD))
                        .child(feature("Оригинальная сетка героев", "WR, PR и число игр поверх родных карточек. Поиск остаётся штатным.", "OpenDota", GREEN))
                        .child(feature("Сборки и история", "Популярные предметы в магазине, матчи и анализ игроков.", "Публичные данные", MUTED)))
                    .child(div().flex().items_center().justify_between().gap_4()
                        .child(div().flex_1().min_w_0().text_size(px(12.)).text_color(rgb(MUTED)).child("Настройки помощника и файл DLL — в разделе «Возможности»."))
                        .child(Button::new("refresh").ghost().small().label("Проверить статус").disabled(busy).on_click(cx.listener(|this, _, _, _| this.send(Command::Refresh)))));
            }
            Page::Modules => {
                content = content.child(heading("Возможности", "Настройка модуля и статус функций."))
                    .child(div().bg(rgb(SURFACE)).rounded_lg().p_5().flex().flex_col().gap_3()
                        .child(div().flex().items_center().justify_between().gap_4()
                            .child(div().flex().flex_col().gap_1()
                                .child(div().font_weight(FontWeight::SEMIBOLD).child("Рекомендованные сборки"))
                                .child(div().text_size(px(12.)).text_color(rgb(MUTED)).child("DotaCoach: сборки по позициям, предметы, навыки и таланты в магазине.")))
                            .child(Switch::new("recommended-builds").checked(recommended)
                                .disabled(!plus || busy || waiting)
                                .on_click(cx.listener(|this, _, _, _| this.send(Command::ToggleRecommendedBuilds)))))
                        .child(div().text_size(px(12.)).text_color(rgb(MUTED)).child("Применяется без перезапуска игры. Если источник ограничил доступ, магазин покажет причину или сохранённый кэш.")))
                    .child(div().bg(rgb(SURFACE)).rounded_lg().p_5().flex().flex_col().gap_3()
                        .child(div().flex().justify_between().items_center().gap_4()
                            .child(div().font_weight(FontWeight::SEMIBOLD).child("Помощник в Dota"))
                            .child(Button::new("plus").label(if plus { "Включён" } else { "Выключен" }).disabled(busy || waiting || loaded)
                                .on_click(cx.listener(|this, _, _, _| this.send(Command::TogglePlus)))))
                        .child(div().text_size(px(12.)).text_color(rgb(MUTED)).child(if loaded { "Чтобы изменить настройку, сначала закрой Dota. Она применяется при загрузке модуля." } else { "Статистика героев, сборки OpenDota, история матчей и дополнительные панели." })))
                    .child(div().flex().flex_col()
                        .child(feature("Мета и сборки", "WR/PR, срезы ранга и линии, популярность предметов. Без ИИ.", "Реализовано", GREEN))
                        .child(feature("Игроки, пики и баны", "Открытая история, серии, частые герои. Роль и смурф — предположение.", "С ограничениями", GOLD))
                        .child(feature("Карта и лес", "Последние видимые варды и курьер, ближайшие видимые лагеря.", "Нужна проверка", GOLD))
                        .child(feature("Локальный тир героя", "Штатный значок и колесо своего героя. Новая реализация требует проверки в игре.", "Нужна проверка", GOLD))
                        .child(feature("Серверные функции Dota Plus", "Рекомендации Valve и подтверждённый прогресс аккаунта недоступны.", "Недоступно", MUTED)))
                    .child(div().pt_2().flex().flex_col().gap_3()
                        .child(div().flex().items_center().justify_between()
                            .child(div().font_weight(FontWeight::SEMIBOLD).child("Файл модуля"))
                            .child(Button::new("choose").small().label("Выбрать DLL").disabled(busy || waiting).on_click(cx.listener(|this, _, _, _| this.send(Command::Pick)))))
                        .child(div().text_size(px(12.)).text_color(rgb(MUTED)).child(path))
                        .child(Button::new("folder").ghost().small().label("Открыть папку модуля").on_click(cx.listener(|this, _, _, _| this.send(Command::Folder)))))
                    .child(div().text_size(px(12.)).text_color(rgb(MUTED)).child("Выбранный файл и настройки сохраняются. Повторное подключение модуля требует перезапуска Dota."));
            }
            Page::Journal => {
                let log = if self.native_log {
                    s.map(|s| s.dll_log.clone()).unwrap_or_default()
                } else {
                    s.map(|s| s.log.iter().cloned().collect::<Vec<_>>().join("\n"))
                        .unwrap_or_default()
                };
                content = content
                    .child(heading("Журнал", "Состояние запуска и сообщения модуля."))
                    .child(
                        div()
                            .flex()
                            .justify_between()
                            .items_center()
                            .gap_3()
                            .child(
                                div()
                                    .flex()
                                    .gap_2()
                                    .child(
                                        Button::new("launcher-log")
                                            .label("Лаунчер")
                                            .selected(!self.native_log)
                                            .on_click(cx.listener(|this, _, _, cx| {
                                                this.native_log = false;
                                                cx.notify();
                                            })),
                                    )
                                    .child(
                                        Button::new("native-log")
                                            .label("Модуль Dota")
                                            .selected(self.native_log)
                                            .on_click(cx.listener(|this, _, _, cx| {
                                                this.native_log = true;
                                                cx.notify();
                                            })),
                                    ),
                            )
                            .child(
                                Button::new("refresh-log")
                                    .ghost()
                                    .label("Обновить")
                                    .disabled(busy)
                                    .on_click(
                                        cx.listener(|this, _, _, _| this.send(Command::Refresh)),
                                    ),
                            ),
                    )
                    .child(
                        div()
                            .bg(rgb(SURFACE))
                            .rounded_lg()
                            .p_4()
                            .text_size(px(12.))
                            .text_color(rgb(MUTED))
                            .child(status),
                    )
                    .child(
                        div()
                            .id("log-lines")
                            .h(px(380.))
                            .overflow_y_scroll()
                            .bg(rgb(SIDE))
                            .border_1()
                            .border_color(rgb(BORDER))
                            .rounded_lg()
                            .p_4()
                            .font_family("Consolas")
                            .text_size(px(12.))
                            .flex()
                            .flex_col()
                            .gap_2()
                            .when(log.is_empty(), |el| {
                                el.child(div().text_color(rgb(MUTED)).child(
                                    "Сообщений пока нет. Журнал появится после запуска модуля.",
                                ))
                            })
                            .children(log.lines().map(|line| {
                                div()
                                    .text_color(rgb(
                                        if line.starts_with("[-]") || line.contains("Ошибка")
                                        {
                                            RED
                                        } else if line.starts_with("[!]") {
                                            GOLD
                                        } else {
                                            MUTED
                                        },
                                    ))
                                    .child(line.to_owned())
                            })),
                    );
            }
        }
        div()
            .size_full()
            .flex()
            .font_family("Segoe UI Variable Text")
            .text_size(px(14.))
            .text_color(rgb(TEXT))
            .bg(rgb(BG))
            .child(sidebar)
            .child(content)
    }
}
