//! The look: shadcn-style dark greys (zinc) and thin borders, the game's yellow for the main buttons, Segoe UI; the
//! title screen far dimmed behind everything (mood, not a focal point), the logo on top.
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
// the colours: shadcn's dark "zinc" (near-black greys, thin borders, quiet text), with the game's yellow as the one
// accent (the main buttons, the room code)
pub const YELLOW: Color32 = Color32::from_rgb(255, 194, 26);
pub const GREEN: Color32 = Color32::from_rgb(74, 222, 128);
pub const RED: Color32 = Color32::from_rgb(248, 113, 113);
pub const DIM_TEXT: Color32 = Color32::from_rgb(161, 161, 170);
const TEXT: Color32 = Color32::from_rgb(250, 250, 250);
const BG: Color32 = Color32::from_rgb(9, 9, 11);
const CARD: Color32 = Color32::from_rgb(24, 24, 27);
const BORDER: Color32 = Color32::from_rgb(39, 39, 42);
const SECONDARY: Color32 = Color32::from_rgb(39, 39, 42);
const SECONDARY_HOVER: Color32 = Color32::from_rgb(63, 63, 70);
const ON_YELLOW: Color32 = Color32::from_rgb(24, 20, 8);
const RADIUS: f32 = 8.0;
/// the semibold face (headings, the main buttons): Segoe UI Semibold
pub fn semibold() -> egui::FontFamily { egui::FontFamily::Name("semibold".into()) }

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

/// A primary button: the yellow accent filled, dark semibold text (shadcn's "default" button).
fn primary(text: &str, size: f32, min: Vec2) -> egui::Button<'static> {
    egui::Button::new(RichText::new(text).size(size).family(semibold()).color(ON_YELLOW)).fill(YELLOW)
        .stroke(egui::Stroke::NONE).rounding(RADIUS).min_size(min)
}

/// A big main-screen button: shadcn's "secondary" (grey, a thin border), large semibold white text; the yellow is
/// kept for what to look at (the room code, the hints), not every button.
pub fn big(ui: &mut egui::Ui, text: &str, enabled: bool) -> egui::Response {
    ui.add_enabled(enabled, egui::Button::new(RichText::new(text).size(16.5).family(semibold()).color(TEXT))
        .min_size(Vec2::new(300.0, 44.0)))
}

/// A section's heading ("Join a room"), centered: a card's title, white semibold.
pub fn section(ui: &mut egui::Ui, title: &str) {
    ui.vertical_centered(|ui| ui.label(RichText::new(title).size(16.0).family(semibold()).color(TEXT)));
}

/// One of two big buttons side by side (a section's choices), filling its column.
pub fn half(ui: &mut egui::Ui, text: &str, enabled: bool) -> egui::Response {
    let w = ui.available_width();
    ui.add_enabled(enabled, primary(text, 16.0, Vec2::new(w, 42.0)))
}

/// shadcn's tabs: a grey rounded strip, the chosen tab a darker raised segment with white text, the others grey
/// text. True when the choice changed.
pub fn tabs(ui: &mut egui::Ui, labels: &[&str], current: &mut usize) -> bool {
    let mut changed = false;
    egui::Frame::none().fill(SECONDARY).rounding(RADIUS + 2.0).inner_margin(4.0).show(ui, |ui| {
        ui.spacing_mut().item_spacing.x = 4.0;
        let w = (ui.available_width() - 4.0 * (labels.len() as f32 - 1.0)) / labels.len() as f32;
        ui.horizontal(|ui| {
            for (i, l) in labels.iter().enumerate() {
                let on = i == *current;
                let text = RichText::new(*l).size(15.0).family(semibold()).color(if on { TEXT } else { DIM_TEXT });
                let b = egui::Button::new(text).min_size(Vec2::new(w, 32.0)).rounding(RADIUS)
                    .fill(if on { BG } else { Color32::TRANSPARENT })
                    .stroke(if on { egui::Stroke::new(1.0_f32, BORDER) } else { egui::Stroke::NONE });
                if ui.add(b).clicked() && !on {
                    *current = i;
                    changed = true;
                }
            }
        });
    });
    changed
}

/// The card the buttons sit on (the dimmed title screen showing a little through).
pub fn panel() -> egui::Frame {
    egui::Frame::none()
        .fill(Color32::from_rgba_unmultiplied(CARD.r(), CARD.g(), CARD.b(), 232))
        .rounding(12.0)
        .inner_margin(20.0)
        .stroke(egui::Stroke::new(1.0_f32, BORDER))
}

/// Windows' own Segoe UI (on every Windows PC: nothing to ship), egui's fonts kept behind it for the symbols (✔ ⚙).
fn fonts(ctx: &egui::Context) {
    let dir = std::env::var_os("WINDIR").map(std::path::PathBuf::from)
        .unwrap_or_else(|| std::path::PathBuf::from(r"C:\Windows")).join("Fonts");
    let load = |n: &str| std::fs::read(dir.join(n)).ok();
    let (Some(regular), Some(semi)) = (load("segoeui.ttf"), load("seguisb.ttf")) else { return };
    let mut f = egui::FontDefinitions::default();
    f.font_data.insert("segoe".into(), egui::FontData::from_owned(regular));
    f.font_data.insert("segoe-sb".into(), egui::FontData::from_owned(semi));
    let fallback = f.families.get(&egui::FontFamily::Proportional).cloned().unwrap_or_default();
    if let Some(p) = f.families.get_mut(&egui::FontFamily::Proportional) {
        p.insert(0, "segoe".into());
    }
    f.families.insert(semibold(), std::iter::once("segoe-sb".to_string()).chain(fallback).collect());
    ctx.set_fonts(f);
}

pub fn style(ctx: &egui::Context) {
    use egui::{Rounding, Stroke};
    fonts(ctx);
    let mut v = egui::Visuals::dark();
    v.panel_fill = BG;
    v.window_fill = BG;
    v.window_stroke = Stroke::new(1.0_f32, BORDER);
    v.window_rounding = Rounding::same(12.0);
    v.window_shadow = egui::epaint::Shadow { offset: Vec2::new(0.0, 8.0), blur: 24.0, spread: 0.0,
                                             color: Color32::from_black_alpha(140) };
    v.popup_shadow = egui::epaint::Shadow { offset: Vec2::new(0.0, 4.0), blur: 12.0, spread: 0.0,
                                            color: Color32::from_black_alpha(120) };
    v.menu_rounding = Rounding::same(RADIUS);
    v.extreme_bg_color = BG; // text boxes
    v.faint_bg_color = CARD;
    v.hyperlink_color = YELLOW;
    v.selection.bg_fill = Color32::from_rgba_unmultiplied(255, 194, 26, 70);
    v.selection.stroke = Stroke::new(1.0_f32, YELLOW);
    v.text_cursor.stroke = Stroke::new(2.0_f32, YELLOW);
    let w = &mut v.widgets;
    w.noninteractive.bg_fill = CARD;
    w.noninteractive.weak_bg_fill = CARD;
    w.noninteractive.bg_stroke = Stroke::new(1.0_f32, BORDER); // separators, frames
    w.noninteractive.fg_stroke = Stroke::new(1.0_f32, Color32::from_rgb(228, 228, 231));
    w.noninteractive.rounding = Rounding::same(RADIUS);
    // secondary buttons, boxes, ticks: a grey fill and a thin border, lighter under the mouse, never growing
    let hover_edge = Color32::from_rgb(82, 82, 91);
    for (st, fill, edge, text) in [(&mut w.inactive, SECONDARY, BORDER, Color32::from_rgb(228, 228, 231)),
                                   (&mut w.hovered, SECONDARY_HOVER, hover_edge, TEXT),
                                   (&mut w.active, SECONDARY_HOVER, YELLOW, TEXT),
                                   (&mut w.open, SECONDARY, BORDER, TEXT)] {
        st.bg_fill = fill;
        st.weak_bg_fill = fill;
        st.bg_stroke = Stroke::new(1.0_f32, edge);
        st.fg_stroke = Stroke::new(1.0_f32, text);
        st.rounding = Rounding::same(RADIUS);
        st.expansion = 0.0;
    }
    ctx.set_visuals(v);
    ctx.style_mut(|s| {
        use egui::{FontFamily, FontId, TextStyle};
        s.spacing.item_spacing = Vec2::new(10.0, 10.0);
        s.spacing.button_padding = Vec2::new(14.0, 7.0);
        s.spacing.interact_size.y = 30.0;
        s.spacing.window_margin = egui::Margin::same(18.0);
        s.text_styles = [
            (TextStyle::Small, FontId::new(12.5, FontFamily::Proportional)),
            (TextStyle::Body, FontId::new(14.5, FontFamily::Proportional)),
            (TextStyle::Button, FontId::new(14.5, FontFamily::Proportional)),
            (TextStyle::Heading, FontId::new(19.0, semibold())),
            (TextStyle::Monospace, FontId::new(13.5, FontFamily::Monospace)),
        ].into();
    });
}
