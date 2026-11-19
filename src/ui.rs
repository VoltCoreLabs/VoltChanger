use crate::{
    appearance::Appearance,
    state::{self, Command, Initialization, Snapshot},
};
use gpui_kit::{
    component::{button::*, switch::Switch, *},
    prelude::FluentBuilder,
    *,
};
use std::{
    sync::mpsc::{Receiver, Sender},
    time::Duration,
};

#[derive(Clone, Copy, PartialEq, Eq)]
enum Page {
    Home,
    Settings,
    Journal,
}

#[derive(Clone, Copy)]
struct Palette {
    text: u32,
    muted: u32,
    blue: u32,
    line: u32,
    dark: bool,
}
impl Palette {
    fn new(dark: bool) -> Self {
        if dark {
            Self {
                text: 0xf0f5ff,
                muted: 0x9daecc,
                blue: 0x76b5ff,
                line: 0x385680,
                dark,
            }
        } else {
            Self {
                text: 0x121a32,
                muted: 0x6e7e9c,
                blue: 0x196aff,
                line: 0xc5d9fa,
                dark,
            }
        }
    }
    fn glass(self) -> Div {
        div()
            .rounded(px(24.))
            .border_1()
            .border_color(tint(
                if self.dark { 0x89b4f5 } else { 0xffffff },
                if self.dark { 48 } else { 220 },
            ))
            .bg(linear_gradient(
                125.,
                linear_color_stop(
                    tint(
                        if self.dark { 0x2b4164 } else { 0xffffff },
                        if self.dark { 165 } else { 170 },
                    ),
                    0.,
                ),
                linear_color_stop(
                    tint(
                        if self.dark { 0x16253d } else { 0xf0f8ff },
                        if self.dark { 150 } else { 115 },
                    ),
                    1.,
                ),
            ))
            .shadow(vec![BoxShadow {
                color: tint(0x2c70ce, if self.dark { 20 } else { 17 }).into(),
                offset: point(px(0.), px(12.)),
                blur_radius: px(42.),
                spread_radius: px(0.),
                inset: false,
            }])
    }
}
fn tint(color: u32, alpha: u32) -> Rgba {
    rgba((color << 8) | alpha)
}
fn glyph(name: &str, side: f32, color: u32) -> impl IntoElement {
    let path = match name {
        "settings" => {
            "<path d='M9.5 3h5l.8 2.8 2.5 1.4 2.7-.6 2.5 4.3-2 2v2.8l2 2-2.5 4.3-2.7-.6-2.5 1.4-.8 2.8h-5l-.8-2.8-2.5-1.4-2.7.6L1 17.7l2-2v-2.8l-2-2 2.5-4.3 2.7.6 2.5-1.4z' transform='translate(1 -1) scale(.92)'/><circle cx='12' cy='12' r='3.4'/>"
        }
        "sun" => {
            "<circle cx='12' cy='12' r='4'/><path d='M12 2v2m0 16v2M2 12h2m16 0h2M5 5l1.5 1.5m11 11L19 19M5 19l1.5-1.5m11-11L19 5'/>"
        }
        "moon" => "<path d='M20 14a8 8 0 0 1-10-10A8.5 8.5 0 1 0 20 14Z'/>",
        "play" => "<path d='m8 4 13 8-13 8z' fill='currentColor' stroke-linejoin='round'/>",
        "layers" => "<path d='m3 7 9-5 9 5-9 5zM3 12l9 5 9-5M3 17l9 5 9-5'/>",
        "monitor" => "<rect x='2' y='3' width='20' height='14' rx='2'/><path d='M8 21h8m-4-4v4'/>",
        "cpu" => {
            "<rect x='6' y='6' width='12' height='12' rx='2'/><path d='M9 1v5m6-5v5M9 18v5m6-5v5M1 9h5m-5 6h5m12-6h5m-5 6h5'/>"
        }
        "check" => "<path d='m5 12 4 4L20 5'/>",
        "info" => "<circle cx='12' cy='12' r='9'/><path d='M12 11v6m0-10v.1'/>",
        "back" => "<path d='m14 5-7 7 7 7'/>",
        "refresh" => "<path d='M20 7a9 9 0 1 0 1 9M20 2v6h-6'/>",
        "folder" => "<path d='M2 7V4h7l3 3h10v13H2z'/>",
        "journal" => "<path d='M6 3h14v18H6zM3 6h5m-5 6h5m-5 6h5M11 8h5m-5 4h5m-5 4h3'/>",
        "close" => "<path d='m6 6 12 12M6 18 18 6'/>",
        "minus" => "<path d='M4 12h16'/>",
        _ => "<circle cx='12' cy='12' r='8'/>",
    };
    let svg = format!(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'>{path}</svg>"
    );
    Icon::default()
        .data(svg.as_bytes())
        .size(px(side))
        .text_color(rgb(color))
}
fn title(text: &'static str, detail: &'static str, p: Palette) -> Div {
    div()
        .flex()
        .flex_col()
        .gap_2()
        .child(
            div()
                .text_size(px(30.))
                .font_weight(FontWeight::BOLD)
                .child(text),
        )
        .child(
            div()
                .text_size(px(14.))
                .text_color(rgb(p.muted))
                .child(detail),
        )
}
fn icon_tile(name: &str, p: Palette) -> Div {
    div()
        .size(px(42.))
        .rounded(px(14.))
        .bg(tint(if p.dark { 0x6d9fe5 } else { 0xbfd9ff }, 45))
        .flex()
        .items_center()
        .justify_center()
        .child(glyph(name, 23., p.blue))
}
fn status_row(
    name: &'static str,
    label: &'static str,
    value: impl Into<SharedString>,
    color: u32,
    p: Palette,
) -> Div {
    div()
        .h(px(48.))
        .flex()
        .items_center()
        .gap_3()
        .child(glyph(name, 20., p.muted))
        .child(div().flex_1().child(label))
        .child(
            div()
                .text_color(rgb(color))
                .font_weight(FontWeight::MEDIUM)
                .child(value.into()),
        )
}
fn setting_text(label: &'static str, detail: &'static str, p: Palette) -> Div {
    div()
        .flex_1()
        .min_w_0()
        .flex()
        .flex_col()
        .gap_2()
        .child(div().font_weight(FontWeight::SEMIBOLD).child(label))
        .child(
            div()
                .text_size(px(13.))
                .text_color(rgb(p.muted))
                .child(detail),
        )
}

pub struct Launcher {
    tx: Sender<Command>,
    rx: Receiver<Snapshot>,
    snapshot: Option<Snapshot>,
    page: Page,
    native_log: bool,
    appearance: Appearance,
    transition: usize,
}
impl Launcher {
    pub fn new(cx: &mut Context<Self>) -> Self {
        let (tx, rx) = state::start();
        let appearance = Appearance::load();
        cx.set_reduce_motion(!appearance.animations);
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
            page: Page::Home,
            native_log: false,
            appearance,
            transition: 0,
        }
    }
    fn send(&self, command: Command) {
        let _ = self.tx.send(command);
    }
    fn navigate(&mut self, page: Page, cx: &mut Context<Self>) {
        self.page = page;
        self.transition += 1;
        cx.notify();
    }
    fn theme(&mut self, dark: bool, cx: &mut Context<Self>) {
        self.appearance.dark = dark;
        self.appearance.save();
        Theme::change(
            if dark {
                ThemeMode::Dark
            } else {
                ThemeMode::Light
            },
            None,
            cx,
        );
        crate::assets::apply_fonts(cx);
        cx.notify();
    }
    fn header(&self, p: Palette, cx: &mut Context<Self>) -> Div {
        let theme = div()
            .rounded_full()
            .p_1()
            .bg(tint(if p.dark { 0x7397ce } else { 0x8eaedb }, 20))
            .flex()
            .gap_1()
            .children(
                [
                    (false, "light", "Светлая", "sun"),
                    (true, "dark", "Тёмная", "moon"),
                ]
                .into_iter()
                .map(|(dark, id, label, icon)| {
                    Button::new(id)
                        .ghost()
                        .label(label)
                        .icon(glyph_icon(icon))
                        .rounded_full()
                        .h(px(34.))
                        .px_3()
                        .bg(if dark == p.dark {
                            tint(if p.dark { 0x6585b5 } else { 0xffffff }, 160)
                        } else {
                            tint(0, 0)
                        })
                        .text_color(rgb(if dark == p.dark { p.blue } else { p.muted }))
                        .on_click(cx.listener(move |this, _, _, cx| this.theme(dark, cx)))
                }),
            );
        div()
            .h(px(82.))
            .flex_shrink_0()
            .flex()
            .items_center()
            .gap_4()
            .px_7()
            .child(
                div()
                    .flex()
                    .items_center()
                    .gap_4()
                    .child(img("volt-icon.png").size(px(49.)))
                    .child(
                        div()
                            .flex()
                            .flex_col()
                            .gap_1()
                            .child(
                                div()
                                    .text_size(px(24.))
                                    .font_weight(FontWeight::BOLD)
                                    .child("VoltChanger"),
                            )
                            .child(
                                div()
                                    .text_size(px(13.))
                                    .text_color(rgb(p.muted))
                                    .child("для Dota 2"),
                            ),
                    ),
            )
            .child(
                div()
                    .ml_3()
                    .px_3()
                    .py_1()
                    .rounded_full()
                    .bg(tint(if p.dark { 0x89b4f5 } else { 0x7b9cc6 }, 22))
                    .text_size(px(12.))
                    .text_color(rgb(p.muted))
                    .child(format!("v{}", env!("CARGO_PKG_VERSION"))),
            )
            .child(
                div()
                    .flex_1()
                    .h_full()
                    .window_control_area(WindowControlArea::Drag),
            )
            .child(theme)
            .child(
                Button::new("minimize")
                    .ghost()
                    .icon(glyph_icon("minus"))
                    .w(px(40.))
                    .h(px(38.))
                    .text_color(rgb(p.muted))
                    .on_click(|_, window, _| window.minimize_window()),
            )
            .child(
                Button::new("close-window")
                    .ghost()
                    .icon(glyph_icon("close"))
                    .w(px(40.))
                    .h(px(38.))
                    .text_color(rgb(p.muted))
                    .on_click(|_, window, _| window.remove_window()),
            )
    }
    fn home(&self, p: Palette, cx: &mut Context<Self>) -> Div {
        let s = self.snapshot.as_ref();
        let busy = s.is_some_and(|s| s.busy || s.auto_running);
        let ready = s.is_some_and(|s| s.loaded && s.initialization == Initialization::Ready);
        let failed = s.is_some_and(|s| {
            !s.valid
                || s.conflicting
                || s.uncertain_pid.is_some()
                || s.initialization == Initialization::Failed
        });
        let loaded = s.is_some_and(|s| s.loaded);
        let game = s.is_some_and(|s| s.pid.is_some());
        let color = if failed {
            0xd95a65
        } else if busy {
            p.blue
        } else {
            if p.dark { 0x53ddb1 } else { 0x079b67 }
        };
        let label = if failed {
            "Проверь настройки"
        } else if busy {
            "Подготовка игры…"
        } else if ready {
            "Всё готово. Можно играть"
        } else if loaded {
            "Модуль инициализируется…"
        } else {
            "Готов к запуску"
        };
        let client = if game {
            "Запущен"
        } else {
            "Не запущен"
        };
        let kernel = if ready {
            "Подключён"
        } else if s.is_some_and(|s| s.initialization == Initialization::Failed) {
            "Ошибка"
        } else if loaded {
            "Инициализация"
        } else if busy {
            "Подключение…"
        } else {
            "Не подключён"
        };
        let helper = if s.is_some_and(|s| s.plus_enabled) {
            "Включён"
        } else {
            "Выключен"
        };
        let pill = div()
            .px_7()
            .py_3()
            .rounded_full()
            .border_1()
            .border_color(tint(0xffffff, 190))
            .bg(tint(
                if failed {
                    0xf8b3bc
                } else if busy {
                    0x96c9ff
                } else {
                    0x86e7c6
                },
                if p.dark { 28 } else { 65 },
            ))
            .text_color(rgb(color))
            .text_size(px(18.))
            .font_weight(FontWeight::SEMIBOLD)
            .flex()
            .items_center()
            .gap_3()
            .child(div().size(px(9.)).rounded_full().bg(rgb(color)))
            .child(label);
        let card = p
            .glass()
            .w_full()
            .max_w(px(790.))
            .p_6()
            .child(
                div()
                    .flex()
                    .items_center()
                    .gap_4()
                    .mb_4()
                    .child(icon_tile("layers", p))
                    .child(
                        div()
                            .flex()
                            .flex_col()
                            .gap_1()
                            .child(
                                div()
                                    .text_size(px(19.))
                                    .font_weight(FontWeight::BOLD)
                                    .child("Статус приложения"),
                            )
                            .child(
                                div()
                                    .text_size(px(13.))
                                    .text_color(rgb(p.muted))
                                    .child("Всё важное о состоянии игры и модуля"),
                            ),
                    ),
            )
            .child(
                div()
                    .flex()
                    .gap_6()
                    .child(
                        div()
                            .flex_1()
                            .min_w_0()
                            .pr_5()
                            .border_r_1()
                            .border_color(tint(p.line, 150))
                            .child(status_row("monitor", "Игровой клиент", client, p.muted, p))
                            .child(div().h(px(1.)).bg(tint(p.line, 110)))
                            .child(status_row("layers", "Помощник в игре", helper, p.muted, p)),
                    )
                    .child(
                        div()
                            .w(px(215.))
                            .flex()
                            .flex_col()
                            .gap_3()
                            .child(
                                div()
                                    .flex()
                                    .items_center()
                                    .gap_3()
                                    .child(icon_tile("info", p))
                                    .child(
                                        div()
                                            .flex()
                                            .flex_col()
                                            .gap_1()
                                            .child(
                                                div()
                                                    .text_size(px(12.))
                                                    .text_color(rgb(p.muted))
                                                    .child("Версия приложения"),
                                            )
                                            .child(
                                                div().font_weight(FontWeight::SEMIBOLD).child(
                                                    format!("v{}", env!("CARGO_PKG_VERSION")),
                                                ),
                                            ),
                                    ),
                            )
                            .child(
                                div()
                                    .flex()
                                    .items_center()
                                    .gap_3()
                                    .child(icon_tile("cpu", p))
                                    .child(
                                        div()
                                            .flex()
                                            .flex_col()
                                            .gap_1()
                                            .child(
                                                div()
                                                    .text_size(px(12.))
                                                    .text_color(rgb(p.muted))
                                                    .child("Нативный модуль"),
                                            )
                                            .child(
                                                div()
                                                    .text_color(rgb(if ready {
                                                        color
                                                    } else {
                                                        p.muted
                                                    }))
                                                    .font_weight(FontWeight::SEMIBOLD)
                                                    .child(kernel),
                                            ),
                                    ),
                            ),
                    ),
            );
        div()
            .flex_1()
            .min_h_0()
            .flex()
            .flex_col()
            .items_center()
            .justify_center()
            .px_8()
            .gap_4()
            .child(img("hero.png").w(px(220.)).h(px(192.)))
            .child(
                div()
                    .flex()
                    .flex_col()
                    .items_center()
                    .gap_2()
                    .mb_2()
                    .child(
                        div()
                            .text_size(px(40.))
                            .font_weight(FontWeight::BOLD)
                            .child("Твоя Dota. Твой стиль."),
                    )
                    .child(
                        div()
                            .text_size(px(20.))
                            .text_color(rgb(p.muted))
                            .child("Любимые образы и сборки в привычной игре"),
                    ),
            )
            .child(pill.with_animation(
                "status-reveal",
                Animation::new(Duration::from_millis(450)).with_easing(ease_out_quint()),
                |el, t| el.opacity(t),
            ))
            .child(card)
            .when(failed, |el| {
                el.child(
                    div()
                        .max_w(px(750.))
                        .text_size(px(12.))
                        .text_color(rgb(0xd95a65))
                        .child(s.map(|s| s.status.clone()).unwrap_or_default()),
                )
            })
            .when(game && !loaded && !busy && !failed, |el| {
                el.child(
                    Button::new("connect-existing")
                        .ghost()
                        .small()
                        .label("Подключить к открытой игре")
                        .disabled(!s.is_some_and(|s| s.game_ready))
                        .on_click(cx.listener(|this, _, _, _| this.send(Command::Load))),
                )
            })
    }
    fn settings(&self, p: Palette, cx: &mut Context<Self>) -> Stateful<Div> {
        let s = self.snapshot.as_ref();
        let loaded = s.is_some_and(|s| s.loaded);
        let busy = s.is_some_and(|s| s.busy || s.auto_running);
        let plus = s.is_some_and(|s| s.plus_enabled);
        let recommended = s.is_some_and(|s| s.recommended_builds);
        div().id("settings-scroll").flex_1().min_h_0().overflow_y_scroll().px_8().py_5()
            .child(div().max_w(px(850.)).mx_auto().flex().flex_col().gap_5()
                .child(title("Настройки", "Настрой игру и внешний вид под себя.", p))
                .child(p.glass().p_6().flex().flex_col().gap_5()
                    .child(div().text_size(px(18.)).font_weight(FontWeight::SEMIBOLD).child("Внутри Dota 2"))
                    .child(div().flex().items_center().gap_6().child(setting_text("Помощник в игре", if loaded { "Для изменения этой настройки сначала закрой Dota 2." } else { "Статистика, визуальный прогресс героев и дополнительные панели." }, p))
                        .child(Switch::new("helper-switch").accessibility_label("Помощник в игре").checked(plus).disabled(busy || loaded)
                            .on_click(cx.listener(|this, _, _, _| this.send(Command::TogglePlus)))))
                    .child(div().h(px(1.)).bg(tint(p.line, 120)))
                    .child(div().flex().items_center().gap_6().child(setting_text("Рекомендованные сборки", "DotaCoach в магазине: позиции, предметы, способности и таланты. Применяется сразу.", p))
                        .child(Switch::new("recommended-switch").accessibility_label("Рекомендованные сборки").checked(recommended).disabled(!plus || busy)
                            .on_click(cx.listener(|this, _, _, _| this.send(Command::ToggleRecommendedBuilds))))))
                .child(p.glass().p_6().flex().flex_col().gap_5()
                    .child(div().text_size(px(18.)).font_weight(FontWeight::SEMIBOLD).child("Внешний вид"))
                    .child(div().flex().items_center().gap_6().child(setting_text("Плавные анимации", "Появление экранов, кнопки и переключатели. Можно отключить для спокойного интерфейса.", p))
                        .child(Switch::new("motion-switch").accessibility_label("Плавные анимации").checked(self.appearance.animations)
                            .on_click(cx.listener(|this, _, _, cx| { this.appearance.animations = !this.appearance.animations; this.appearance.save(); cx.set_reduce_motion(!this.appearance.animations); cx.notify(); })))))
                .child(p.glass().p_6().flex().flex_col().gap_4()
                    .child(div().flex().justify_between().items_center().child(div().text_size(px(18.)).font_weight(FontWeight::SEMIBOLD).child("Файл модуля"))
                        .child(Button::new("choose-dll").label("Выбрать DLL").disabled(busy).on_click(cx.listener(|this, _, _, _| this.send(Command::Pick)))))
                    .child(div().text_size(px(12.)).text_color(rgb(p.muted)).child(s.map(|s| s.dll.display().to_string()).unwrap_or_default()))
                    .child(div().flex().gap_3().child(Button::new("open-folder").ghost().label("Открыть папку").on_click(cx.listener(|this, _, _, _| this.send(Command::Folder))))
                        .child(Button::new("open-journal").ghost().label("Журнал приложения").on_click(cx.listener(|this, _, _, cx| this.navigate(Page::Journal, cx))))))
                .child(div().text_size(px(12.)).text_color(rgb(p.muted)).pb_4().px_2().child("Предметы и экипировка сохраняются автоматически. Визуальные изменения работают локально.")))
    }
    fn journal(&self, p: Palette, cx: &mut Context<Self>) -> Div {
        let s = self.snapshot.as_ref();
        let log = if self.native_log {
            s.map(|s| s.dll_log.clone()).unwrap_or_default()
        } else {
            s.map(|s| s.log.iter().cloned().collect::<Vec<_>>().join("\n"))
                .unwrap_or_default()
        };
        div()
            .flex_1()
            .min_h_0()
            .px_8()
            .py_5()
            .flex()
            .flex_col()
            .gap_5()
            .child(title(
                "Журнал приложения",
                "Состояние запуска и сообщения нативного модуля.",
                p,
            ))
            .child(
                div()
                    .flex()
                    .justify_between()
                    .child(
                        div()
                            .flex()
                            .gap_2()
                            .child(
                                Button::new("app-log")
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
                        Button::new("refresh")
                            .ghost()
                            .label("Обновить")
                            .on_click(cx.listener(|this, _, _, _| this.send(Command::Refresh))),
                    ),
            )
            .child(
                p.glass()
                    .flex_1()
                    .min_h_0()
                    .id("journal-lines")
                    .overflow_y_scroll()
                    .p_5()
                    .font_family(crate::assets::FONT_FAMILY)
                    .text_size(px(12.))
                    .flex()
                    .flex_col()
                    .gap_2()
                    .when(log.is_empty(), |el| el.child("Сообщений пока нет."))
                    .children(log.lines().map(|line| {
                        div()
                            .text_color(rgb(
                                if line.starts_with("[-]") || line.contains("Ошибка") {
                                    0xd95a65
                                } else {
                                    p.muted
                                },
                            ))
                            .child(line.to_owned())
                    })),
            )
    }
    fn footer(&self, p: Palette, cx: &mut Context<Self>) -> Div {
        let s = self.snapshot.as_ref();
        let busy = s.is_some_and(|s| s.busy || s.auto_running);
        let loaded = s.is_some_and(|s| s.loaded);
        let enabled = s.is_some_and(|s| {
            s.valid
                && !s.loaded
                && !s.conflicting
                && !s.busy
                && !s.auto_running
                && s.uncertain_pid.is_none()
        });
        let primary = ButtonCustomVariant::new(cx)
            .color(rgb(0x2477ff).into())
            .foreground(rgb(0xffffff).into())
            .hover(rgb(0x418eff).into())
            .active(rgb(0x1659d7).into());
        div()
            .h(px(90.))
            .flex_shrink_0()
            .px_7()
            .pb_5()
            .flex()
            .items_center()
            .justify_between()
            .child(
                Button::new("settings-nav")
                    .ghost()
                    .icon(glyph_icon(if self.page == Page::Home {
                        "settings"
                    } else {
                        "back"
                    }))
                    .label(if self.page == Page::Home {
                        "Настройки"
                    } else {
                        "На главную"
                    })
                    .h(px(52.))
                    .px_5()
                    .rounded(px(16.))
                    .border_1()
                    .border_color(tint(0xffffff, 180))
                    .bg(tint(
                        if p.dark { 0x6386b9 } else { 0xffffff },
                        if p.dark { 25 } else { 90 },
                    ))
                    .text_color(rgb(p.text))
                    .on_click(cx.listener(|this, _, _, cx| {
                        this.navigate(
                            if this.page == Page::Home {
                                Page::Settings
                            } else {
                                Page::Home
                            },
                            cx,
                        )
                    })),
            )
            .child(
                div()
                    .flex()
                    .items_center()
                    .gap_3()
                    .when(busy, |el| {
                        el.child(
                            Button::new("cancel-launch")
                                .ghost()
                                .label("Отменить ожидание")
                                .on_click(cx.listener(|this, _, _, _| this.send(Command::Cancel))),
                        )
                    })
                    .child(
                        Button::new("launch")
                            .with_variant(ButtonVariant::Custom(primary))
                            .icon(glyph_icon(if loaded { "check" } else { "play" }))
                            .label(if busy {
                                "Подготовка…"
                            } else if loaded {
                                "Подключено"
                            } else {
                                "Запустить"
                            })
                            .disabled(!enabled)
                            .h(px(56.))
                            .w(px(228.))
                            .rounded(px(17.))
                            .text_size(px(20.))
                            .font_weight(FontWeight::BOLD)
                            .shadow(vec![BoxShadow {
                                color: tint(0x2075ee, 60).into(),
                                offset: point(px(0.), px(8.)),
                                blur_radius: px(24.),
                                spread_radius: px(0.),
                                inset: false,
                            }])
                            .on_click(cx.listener(|this, _, _, _| this.send(Command::Start))),
                    ),
            )
    }
}
fn glyph_icon(name: &str) -> Icon {
    // Buttons use the same original line icon set as the larger status tiles.
    let path = match name {
        "sun" => {
            "<circle cx='12' cy='12' r='4'/><path d='M12 2v2m0 16v2M2 12h2m16 0h2M5 5l1 1m12 12 1 1M5 19l1-1M18 6l1-1'/>"
        }
        "moon" => "<path d='M20 14a8 8 0 0 1-10-10A8.5 8.5 0 1 0 20 14Z'/>",
        "play" => "<path d='m8 4 13 8-13 8Z' fill='currentColor'/>",
        "check" => "<path d='m5 12 4 4L20 5'/>",
        "close" => "<path d='m6 6 12 12M6 18 18 6'/>",
        "minus" => "<path d='M4 12h16'/>",
        "back" => "<path d='m14 5-7 7 7 7'/>",
        _ => {
            "<path d='m9 3 .7 2.5-2 2L5 7l-2 4 2 2v2l-1 2 3 3 2-1 2 1 1 2h4l1-3 2-1 2 1 2-4-2-2v-2l1-2-3-3-2 1-2-1L14 3Z'/><circle cx='12' cy='12' r='3'/>"
        }
    };
    Icon::default().data(format!("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'>{path}</svg>").as_bytes())
}
impl Render for Launcher {
    fn render(&mut self, _: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let p = Palette::new(self.appearance.dark);
        let content = match self.page {
            Page::Home => self.home(p, cx),
            Page::Settings => div()
                .flex_1()
                .min_h_0()
                .flex()
                .flex_col()
                .child(self.settings(p, cx)),
            Page::Journal => self.journal(p, cx),
        };
        div()
            .size_full()
            .relative()
            .overflow_hidden()
            .rounded(px(22.))
            .border_1()
            .border_color(tint(if p.dark { 0x6592cf } else { 0x94baff }, 140))
            .bg(tint(if p.dark { 0x0d172b } else { 0xedf5ff }, 240))
            .font_family(crate::assets::FONT_FAMILY)
            .text_size(px(14.))
            .text_color(rgb(p.text))
            .child(
                img("background.png")
                    .absolute()
                    .size_full()
                    .object_fit(ObjectFit::Fill)
                    .opacity(if p.dark { 0.24 } else { 0.9 }),
            )
            .child(
                div()
                    .relative()
                    .size_full()
                    .flex()
                    .flex_col()
                    .child(self.header(p, cx))
                    .child(content.with_animation(
                        ("page", self.transition),
                        Animation::new(Duration::from_millis(260)).with_easing(ease_out_quint()),
                        |el, t| el.opacity(t),
                    ))
                    .child(self.footer(p, cx)),
            )
    }
}
