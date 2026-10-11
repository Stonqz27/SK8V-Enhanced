//! Skate 3's Trick Analyzer (the APT `trickanalyser` movie): after each
//! trick, the stick gesture that makes it. tools/prepare-hom-hud.py --movie
//! trickanalyser merges the gesture_item control's `g_<gesture>` diagrams in;
//! the movie's own script fades them in and out. Same VM and natives as the
//! Hall of Meat movie, plus `HUDComponents.TrickAnalyzer_GetTrickData` (the
//! gesture set by [`AnalyserPlayer::gesture`]).
//!
//! The movie's `mTrickAnalyzer` is an empty 100-unit box over the stick ring,
//! where the game drew the player's own stick movement: [`flick`] cuts the
//! last flick out of the right stick's samples and [`draw_path`] replays it
//! there over the diagram.
use crate::apt_vm::Value;
use crate::hom::Placement;
use crate::layout::Layout;
use crate::movie::MoviePlayer;
use crate::player::{Assets, DrawCmd, Frame, Vertex};

/// `trickanalyser.SetState` arguments (movie script).
mod state {
    pub const SHOW: i32 = 1;
    pub const HIDE: i32 = 2;
    pub const UPDATE_GESTURE: i32 = 3;
}

pub struct AnalyserPlayer {
    core: MoviePlayer,
    controller: usize,
}

impl AnalyserPlayer {
    pub fn new(assets: &Assets) -> Result<Self, String> {
        let core = MoviePlayer::new(assets, "Trick Analyzer")?;
        let Value::Object(controller) = core.vm.get(core.bindings.movie.root, "screen") else {
            return Err("Trick Analyzer controller was not constructed".into());
        };
        let mut p = Self { core, controller };
        p.set_state(state::SHOW)?;
        Ok(p)
    }

    fn set_state(&mut self, state: i32) -> Result<(), String> {
        let core = &mut self.core;
        core.vm.begin_update();
        core.vm.call_method(self.controller, "SetState", vec![Value::Number(state as f64)], &mut core.bindings)?;
        core.drain()
    }

    /// Shows `gesture` (a gesture_item name without `g_`, see [`gesture_for`]),
    /// `mirrored` while the rider stands goofy (retail's `_xscale = -100`, the
    /// diagrams being drawn for regular); "" fades the last one out.
    pub fn gesture(&mut self, gesture: &str, mirrored: bool) -> Result<(), String> {
        self.core.bindings.gesture = gesture.into();
        self.core.bindings.mirrored = mirrored;
        self.set_state(state::UPDATE_GESTURE)
    }

    pub fn hide(&mut self) -> Result<(), String> {
        self.set_state(state::HIDE)
    }

    /// One timeline frame.
    pub fn advance(&mut self) -> Result<(), String> {
        self.core.advance()
    }

    /// The movie's scene in `layout`'s pixels, moved to [`placement`].
    pub fn frame(&self, assets: &Assets, layout: &Layout) -> Result<Frame, String> {
        let mut frame = self.core.native_frame(assets, layout)?;
        let place = placement(layout);
        for v in &mut frame.vertices {
            [v.x, v.y] = place.apply([v.x, v.y]);
        }
        Ok(frame)
    }

    pub fn visible_text(&self) -> Vec<String> {
        self.core.visible_text()
    }
}

/// `mTrickAnalyzer`'s centre and half size (its +-50 box at 0.95), movie units.
const PATH_CENTRE: [f32; 2] = [640.0, 571.5];
const PATH_RADIUS: f32 = 47.5;
const PATH_WIDTH: f32 = 3.0;
const PATH_COLOUR: [f32; 4] = [1.0, 0.62, 0.12, 0.95];
/// The trick display's multiplier ring, movie units before its edge offset
/// (measured from the rendered movie), and the room kept above it.
const MULTIPLIER_CENTRE: [f32; 2] = [1124.6, 611.0];
const MULTIPLIER_RADIUS: f32 = 42.0;
/// Retail sits the analyser at the bottom centre, full size; SK8V shows it
/// smaller, just above the multiplier ring, where the trick lines (right
/// aligned left of the ring, stacking upwards) never reach.
const SCALE: f32 = 0.6;
/// The stick ring's radius at the end of its intro (Gesture Item at 1.75).
const RING_RADIUS: f32 = 55.0;

pub fn placement(layout: &Layout) -> Placement {
    let from = layout.to_pixels(PATH_CENTRE);
    let gap = 6.0;
    let centre = [
        MULTIPLIER_CENTRE[0] - layout.edge_offset,
        MULTIPLIER_CENTRE[1] - MULTIPLIER_RADIUS - gap - RING_RADIUS * SCALE,
    ];
    Placement { from, to: layout.to_pixels(centre), scale: SCALE }
}

/// Stick deflection below which the stick counts as at rest.
const REST: f32 = 0.25;

/// The last flick in `samples` (oldest first, -1..1, y up): the stick's last
/// excursion from rest, with the rest sample it left from. Empty if the stick
/// never left rest.
pub fn flick(samples: &[[f32; 2]]) -> Vec<[f32; 2]> {
    let away = |p: &[f32; 2]| p[0].hypot(p[1]) > REST;
    let Some(last) = samples.iter().rposition(away) else { return Vec::new() };
    let start = samples[..last].iter().rposition(|p| !away(p)).unwrap_or(0);
    samples[start..=last].to_vec()
}

/// Appends `path` (stick samples, -1..1, y up) to `frame` as line segments
/// over the stick ring. `texture` is a white texel in the frame's set.
pub fn draw_path(frame: &mut Frame, layout: &Layout, path: &[[f32; 2]], texture: u32) {
    let place = placement(layout);
    let point = |p: &[f32; 2]| {
        let [x, y] = [p[0].clamp(-1.0, 1.0), p[1].clamp(-1.0, 1.0)];
        place.apply(layout.to_pixels([PATH_CENTRE[0] + x * PATH_RADIUS, PATH_CENTRE[1] - y * PATH_RADIUS]))
    };
    let half = PATH_WIDTH * layout.scale * 0.5 / SCALE.sqrt();
    let first = frame.vertices.len() as u32;
    for pair in path.windows(2) {
        let (a, b) = (point(&pair[0]), point(&pair[1]));
        let (dx, dy) = (b[0] - a[0], b[1] - a[1]);
        let len = dx.hypot(dy);
        if len < 1e-3 {
            continue;
        }
        // Extended by half the width at both ends so the joins close.
        let (ux, uy) = (dx / len * half, dy / len * half);
        let (nx, ny) = (-uy, ux);
        let v = |x: f32, y: f32| Vertex { x, y, u: 0.5, v: 0.5 };
        let (a, b) = ([a[0] - ux, a[1] - uy], [b[0] + ux, b[1] + uy]);
        let quad = [
            v(a[0] + nx, a[1] + ny),
            v(b[0] + nx, b[1] + ny),
            v(b[0] - nx, b[1] - ny),
            v(a[0] + nx, a[1] + ny),
            v(b[0] - nx, b[1] - ny),
            v(a[0] - nx, a[1] - ny),
        ];
        frame.vertices.extend_from_slice(&quad);
    }
    let count = frame.vertices.len() as u32 - first;
    if count > 0 {
        frame.draws.push(DrawCmd { texture, first, count, multiply: PATH_COLOUR, add: [0.0; 4] });
    }
}

/// The gesture_item diagram for a scorable identifier (Skate's scoring
/// catalog): flip tricks by their pop (`n_` nollie, else ollie), late shuvits
/// by the stance they were announced in, grinds and slides by their entry.
/// "" for tricks without a diagram (laser flips, late flips, grabs, manuals).
pub fn gesture_for(trick: &str, nollie: bool) -> String {
    let (pop, base) = match trick.strip_prefix("n_") {
        Some(rest) => ("nollie", rest),
        None if trick == "nollie" => ("nollie", ""),
        None => ("ollie", trick),
    };
    // Double/triple flips, dark catches and underflips share the base flick.
    let mut base = base.trim_end_matches("_darkcatch");
    let under = base.strip_suffix("_underflip").inspect(|b| base = b).is_some();
    base = base.trim_end_matches(['2', '3', '4']);
    if under {
        return match base {
            "kickflip" | "heelflip" => format!("{pop}_underflip_{base}"),
            "popshuvit" | "fspopshuvit" => format!("{pop}_underflip_shuv"),
            _ => String::new(),
        };
    }
    let late = if nollie { "nollie" } else { "ollie" };
    let name: &str = match base {
        "" | "ollie" => return pop.into(),
        "kickflip" | "heelflip" | "hardflip" => base,
        "popshuvit" | "360popshuvit" => base,
        "fspopshuvit" => "fs_popshuvit",
        "fs360popshuvit" => "fs_360popshuvit",
        "varialkickflip" if pop == "nollie" => "varial_flip",
        "varialkickflip" => "varial_kickflip",
        "varialheelflip" => "varial_heelflip",
        "inwardheelflip" => "inward_heelflip",
        "360flip" => "360_flip",
        "360hardflip" => "360_hardflip",
        "360inwardheelflip" if pop == "nollie" => "360_inward_heel",
        "360inwardheelflip" => "360_inward_heelflip",
        "latebsshuv" => return format!("{late}_popshuvit_late"),
        "latefsshuv" => return format!("{late}_fs_popshuvit_late"),
        "fsrevert" | "bsrevert" => return "revert".into(),
        _ => return grind(base).map_or_else(String::new, |g| format!("grind_{g}")),
    };
    format!("{pop}_{name}")
}

fn grind(trick: &str) -> Option<&'static str> {
    Some(match trick {
        "grind5_o" => "5_0_grind",
        "grindnose" => "nose_grind",
        "grindcrooks" => "fs_crooks",
        "grindovercrooks" => "fs_overcrooks",
        "grindfeeble" => "fs_feeble",
        "grindsmith" => "fs_smith",
        "grindsalad" => "fs_salad",
        "grindwilly" => "fs_willie",
        "grindoverwilly" => "fs_overwillie",
        "slideblunt" => "fs_bluntslide",
        "slidenblunt" => "fs_noseblunt",
        "slidetail" => "fs_tailslide",
        "slidenose" => "fs_noseslide",
        "slidelip" => "fs_lipslide",
        "slideboard" => "fs_boardslide",
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_last_flick_is_cut_from_rest() {
        let rest = [0.0, 0.0];
        let s = [rest, [0.0, -0.9], [0.0, -1.0], rest, rest, [0.0, -0.8], [0.5, 0.2], [0.7, 0.9], rest, rest];
        assert_eq!(flick(&s), vec![rest, [0.0, -0.8], [0.5, 0.2], [0.7, 0.9]]);
        assert!(flick(&[rest, [0.1, 0.1]]).is_empty());
        let l = Layout::compute(1920, 1080, 1.0, crate::layout::DEFAULT_MAX_ASPECT);
        let mut f = Frame::default();
        draw_path(&mut f, &l, &flick(&s), 7);
        assert_eq!((f.draws.len(), f.vertices.len()), (1, 18));
        assert_eq!(f.draws[0].texture, 7);
    }

    #[test]
    fn identifiers_map_to_gesture_diagrams() {
        for (id, nollie, want) in [
            ("ollie", false, "ollie"),
            ("nollie", true, "nollie"),
            ("kickflip", false, "ollie_kickflip"),
            ("kickflip3_darkcatch", false, "ollie_kickflip"),
            ("n_heelflip2_underflip", true, "nollie_underflip_heelflip"),
            ("fspopshuvit_underflip", false, "ollie_underflip_shuv"),
            ("n_varialkickflip", true, "nollie_varial_flip"),
            ("n_360inwardheelflip_darkcatch", true, "nollie_360_inward_heel"),
            ("360inwardheelflip", false, "ollie_360_inward_heelflip"),
            ("fs360popshuvit", false, "ollie_fs_360popshuvit"),
            ("latefsshuv", true, "nollie_fs_popshuvit_late"),
            ("bsrevert", false, "revert"),
            ("grindcrooks", false, "grind_fs_crooks"),
            ("slidenblunt", false, "grind_fs_noseblunt"),
            ("laserflip", false, ""),
            ("latekickflip", false, ""),
            ("bs_50_50", false, ""),
        ] {
            assert_eq!(gesture_for(id, nollie), want, "{id}");
        }
    }

    /// Retail movie (local data only): a kickflip's diagram fades in, then out.
    #[test]
    fn shows_the_gesture() {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../local/skate-data/assets");
        let Ok(a) = Assets::load_analyser(&Assets::analyser_dir_for(&root)) else { return };
        let l = Layout::compute(1920, 1080, 1.0, crate::layout::DEFAULT_MAX_ASPECT);
        let mut p = AnalyserPlayer::new(&a).unwrap();
        for _ in 0..20 {
            p.advance().unwrap();
        }
        let idle = p.frame(&a, &l).unwrap().draws.len();
        p.gesture("ollie_kickflip", true).unwrap();
        for _ in 0..45 {
            p.advance().unwrap();
        }
        let mut shown = p.frame(&a, &l).unwrap();
        assert!(shown.draws.len() > idle, "{idle} draws idle, {} with the gesture", shown.draws.len());
        // SKATEV_HUD_PNG_DIR: analyser.png.
        if let Some(dir) = std::env::var_os("SKATEV_HUD_PNG_DIR") {
            let mut textures: Vec<_> = a.textures.iter().map(crate::raster::LinearTexture::from).collect();
            let white = crate::player::Texture { name: "white".into(), width: 1, height: 1, rgba: vec![255; 4] };
            textures.push(crate::raster::LinearTexture::from(&white));
            let kickflip = [[0.0, 0.0], [0.0, -0.6], [0.05, -1.0], [0.3, -0.4], [0.55, 0.3], [0.7, 0.9]];
            draw_path(&mut shown, &l, &kickflip, textures.len() as u32 - 1);
            let mut img = crate::raster::Image::new(1920, 1080, [0.3, 0.35, 0.3, 1.0]);
            crate::raster::composite(&mut img, &crate::raster::render_target(&shown, &textures));
            let file = std::fs::File::create(std::path::Path::new(&dir).join("analyser.png")).unwrap();
            let mut e = png::Encoder::new(std::io::BufWriter::new(file), 1920, 1080);
            e.set_color(png::ColorType::Rgba);
            e.set_depth(png::BitDepth::Eight);
            e.write_header().unwrap().write_image_data(&img.to_rgba8()).unwrap();
        }
        p.gesture("", false).unwrap();
        for _ in 0..20 {
            p.advance().unwrap();
        }
        p.hide().unwrap();
    }
}
