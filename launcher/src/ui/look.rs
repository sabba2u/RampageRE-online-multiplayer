//! The look: the title screen far dimmed behind everything (mood, not a focal point), the logo on top, big buttons.
use eframe::egui::{self, Color32, Rect, RichText, Vec2};

pub const BACKGROUND: &[u8] = include_bytes!("../../assets/background.jpg");
pub const LOGO: &[u8] = include_bytes!("../../assets/logo.png");
/// the tint multiplies in gamma space: 30 of 255 shows about as dark as the games page's brightness(.22)
const DIM: u8 = 30;
pub const YELLOW: Color32 = Color32::from_rgb(255, 194, 26);
pub const GREEN: Color32 = Color32::from_rgb(110, 214, 110);
pub const RED: Color32 = Color32::from_rgb(240, 90, 70);
pub const DIM_TEXT: Color32 = Color32::from_rgb(190, 182, 170);

/// The background, covering the window (cropped, never stretched).
pub fn background(ui: &mut egui::Ui) {
    let full = ui.max_rect();
    let aspect = 1280.0 / 720.0;
    let size = if full.width() / full.height() > aspect {
        Vec2::new(full.width(), full.width() / aspect)
    } else {
        Vec2::new(full.height() * aspect, full.height())
    };
    let rect = Rect::from_center_size(full.center(), size);
    egui::Image::from_bytes("bytes://background.jpg", BACKGROUND).tint(Color32::from_gray(DIM)).paint_at(ui, rect);
}

pub fn logo(ui: &mut egui::Ui, width: f32) {
    ui.add(egui::Image::from_bytes("bytes://logo.png", LOGO).fit_to_exact_size(Vec2::new(width, width / 3.0)));
}

/// A big main-screen button.
pub fn big(ui: &mut egui::Ui, text: &str, enabled: bool) -> egui::Response {
    ui.add_enabled(enabled, egui::Button::new(RichText::new(text).size(20.0).strong()).min_size(Vec2::new(300.0, 46.0)))
}

/// The dark panel the buttons sit on.
pub fn panel() -> egui::Frame {
    egui::Frame::none()
        .fill(Color32::from_rgba_unmultiplied(20, 18, 16, 215))
        .rounding(10.0)
        .inner_margin(18.0)
        .stroke(egui::Stroke::new(1.0, Color32::from_rgb(58, 53, 48)))
}

pub fn style(ctx: &egui::Context) {
    ctx.set_visuals(egui::Visuals::dark());
    ctx.style_mut(|s| {
        s.spacing.item_spacing = Vec2::new(10.0, 10.0);
        s.spacing.button_padding = Vec2::new(14.0, 8.0);
    });
}
