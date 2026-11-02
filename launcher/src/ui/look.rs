//! The look: the title screen far dimmed behind everything (mood, not a focal point), the logo on top, big buttons.
use eframe::egui::{self, Color32, Rect, RichText, Vec2};

pub const BACKGROUND: &[u8] = include_bytes!("../../assets/background.jpg");
pub const LOGO: &[u8] = include_bytes!("../../assets/logo.png");
/// the window's and the taskbar's icon (the owner's Boss Rumble art, images/bossrush.png; build.rs puts assets/icon.ico
/// on the exe)
pub const ICON: &[u8] = include_bytes!("../../assets/icon.png");

pub fn icon() -> Option<egui::IconData> {
    let img = image::load_from_memory(ICON).ok()?.into_rgba8();
    Some(egui::IconData { width: img.width(), height: img.height(), rgba: img.into_raw() })
}
/// the tint multiplies in gamma space: 30 of 255 shows about as dark as the games page's brightness(.22)
const DIM: u8 = 30;
/// the DirectX / Vulkan fallback blends the tint brighter: this matches DIM under OpenGL
const DIM_WGPU: u8 = 21;
static WGPU: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

/// The window is drawn with the DirectX / Vulkan fallback (the background dims to match).
pub fn using_wgpu(on: bool) { WGPU.store(on, std::sync::atomic::Ordering::Relaxed); }
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
    // cropped toward the right: George climbing the Sears Tower on the right edge stays in view (the lizard on the
    // left is wide enough to lose a little)
    let spare = size.x - full.width();
    let rect = Rect::from_min_size(egui::pos2(full.min.x - spare * 0.85, full.center().y - size.y / 2.0), size);
    let dim = if WGPU.load(std::sync::atomic::Ordering::Relaxed) { DIM_WGPU } else { DIM };
    egui::Image::from_bytes("bytes://background.jpg", BACKGROUND).tint(Color32::from_gray(dim)).paint_at(ui, rect);
}

pub fn logo(ui: &mut egui::Ui, width: f32) {
    ui.add(egui::Image::from_bytes("bytes://logo.png", LOGO).fit_to_exact_size(Vec2::new(width, width / 3.0)));
}

/// A big main-screen button.
pub fn big(ui: &mut egui::Ui, text: &str, enabled: bool) -> egui::Response {
    ui.add_enabled(enabled, egui::Button::new(RichText::new(text).size(20.0).strong()).min_size(Vec2::new(300.0, 46.0)))
}

/// A section's heading ("HOST A GAME"), centered.
pub fn section(ui: &mut egui::Ui, title: &str) {
    ui.vertical_centered(|ui| ui.label(RichText::new(title.to_uppercase()).size(15.0).strong().color(YELLOW)));
}

/// One of two big buttons side by side (a section's choices), filling its column.
pub fn half(ui: &mut egui::Ui, text: &str, enabled: bool) -> egui::Response {
    let w = ui.available_width();
    ui.add_enabled(enabled, egui::Button::new(RichText::new(text).size(18.0).strong()).min_size(Vec2::new(w, 46.0)))
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
