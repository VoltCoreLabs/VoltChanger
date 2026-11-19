#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
#[cfg(not(all(target_os = "windows", target_arch = "x86_64")))]
compile_error!("VoltChanger requires Windows x64");
mod autoplay;
mod native;
mod state;
mod ui;

fn main() {
    use gpui_kit::*;
    gpui_kit::application()
        .with_assets(gpui_kit::assets::Assets)
        .run(|cx| {
            gpui_kit::init(cx);
            gpui_kit::component::Theme::change(gpui_kit::component::ThemeMode::Dark, None, cx);
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
                        size(px(1080.), px(800.)),
                        cx,
                    ))),
                    window_min_size: Some(size(px(940.), px(660.))),
                    titlebar: Some(TitlebarOptions {
                        title: Some("VoltChanger • Dota 2".into()),
                        ..Default::default()
                    }),
                    ..Default::default()
                },
                cx,
                |_, cx| cx.new(ui::Launcher::new),
            )
            .expect("Не удалось открыть окно VoltChanger");
            cx.activate(true);
        });
}
