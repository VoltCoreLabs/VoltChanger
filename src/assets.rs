use gpui_kit::{App, AssetSource, SharedString, component::Theme};
use std::borrow::Cow;

pub const FONT_FAMILY: &str = "Golos Text";

pub fn init_fonts(cx: &mut App) {
    cx.text_system()
        .add_fonts(vec![Cow::Borrowed(include_bytes!(
            "../resources/fonts/GolosText-Variable.ttf"
        ))])
        .expect("Не удалось загрузить встроенный Golos Text");
}

pub fn apply_fonts(cx: &mut App) {
    Theme::update(cx, |theme| {
        theme.font_family = FONT_FAMILY.into();
        theme.mono_font_family = FONT_FAMILY.into();
    });
}

pub struct Assets;
impl AssetSource for Assets {
    fn load(&self, path: &str) -> anyhow::Result<Option<Cow<'static, [u8]>>> {
        let data: Option<&'static [u8]> = match path {
            "volt-icon.png" => Some(include_bytes!("../resources/launcher/volt-icon.png")),
            "hero.png" => Some(include_bytes!("../resources/launcher/hero.png")),
            "background.png" => Some(include_bytes!("../resources/launcher/background.png")),
            _ => None,
        };
        if let Some(data) = data {
            Ok(Some(Cow::Borrowed(data)))
        } else {
            gpui_kit::assets::Assets.load(path)
        }
    }
    fn list(&self, path: &str) -> anyhow::Result<Vec<SharedString>> {
        let mut paths = gpui_kit::assets::Assets.list(path)?;
        paths.extend(
            ["volt-icon.png", "hero.png", "background.png"]
                .into_iter()
                .filter(|p| p.starts_with(path))
                .map(Into::into),
        );
        Ok(paths)
    }
}
