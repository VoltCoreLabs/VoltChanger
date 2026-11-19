#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
#[cfg(not(all(target_os = "windows", target_arch = "x86_64")))]
compile_error!("VoltChanger requires Windows x64");
mod appearance;
mod assets;
mod autoplay;
mod native;
mod state;
mod ui;

fn main() {
    use gpui_kit::*;
    gpui_kit::application()
        .with_assets(crate::assets::Assets)
        .run(|cx| {
            gpui_kit::init(cx);
            gpui_kit::component::Theme::change(
                if appearance::Appearance::load().dark {
                    gpui_kit::component::ThemeMode::Dark
                } else {
                    gpui_kit::component::ThemeMode::Light
                },
                None,
                cx,
            );
            cx.on_window_closed(|cx, _| {
                if cx.windows().is_empty() {
                    cx.quit();
                }
            })
            .detach();
            gpui_kit::open_window(
                WindowOptions {
                    window_bounds: Some(WindowBounds::Windowed(Bounds::centered(
                        None,
                        size(px(1180.), px(820.)),
                        cx,
                    ))),
                    window_min_size: Some(size(px(1020.), px(760.))),
                    titlebar: None,
                    window_background: WindowBackgroundAppearance::Blurred,
                    app_id: Some("VoltCoreLabs.VoltChanger".into()),
                    ..Default::default()
                },
                cx,
                |window, cx| {
                    window.set_window_title("VoltChanger");
                    cx.new(ui::Launcher::new)
                },
            )
            .expect("Не удалось открыть окно VoltChanger");
            cx.activate(true);
        });
}
