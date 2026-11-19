use gpui_kit::{AssetSource, SharedString};
use std::borrow::Cow;

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
