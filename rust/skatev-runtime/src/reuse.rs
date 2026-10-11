//! What changed between two area builds, so the next build redoes the
//! expensive per-item work (rails, curb bevels, ply filters) only where it
//! could come out different.
//!
//! Every item those stages output (a candidate edge, a triangle) depends only
//! on triangles within a fixed reach of it (each stage names its `pad`). A
//! triangle that appeared or disappeared since the last build marks the
//! cells its box covers; an item whose box, widened by its stage's pad,
//! touches no marked cell reads exactly what it read last time and keeps its
//! last result. GTA streaming a prop in near the skater, or the area moving
//! on, then rebuilds a strip instead of the whole 256 m square.
use bevy_math::Vec3;
use rustc_hash::{FxHashMap, FxHashSet};

/// Side of a changed-region cell (metres).
const CELL: f32 = 4.0;

/// A triangle's identity across builds: its vertex bits, mixed to 64 bits.
pub fn tri_key(t: &[Vec3; 3]) -> u64 {
    let mut h = 0x9E37_79B9_7F4A_7C15u64;
    for v in t {
        for x in v.to_array() {
            h = mix(h ^ x.to_bits() as u64);
        }
    }
    h
}

/// splitmix64's finaliser: every input bit moves every output bit.
pub fn mix(mut h: u64) -> u64 {
    h = h.wrapping_add(0x9E37_79B9_7F4A_7C15);
    h = (h ^ (h >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    h = (h ^ (h >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    h ^ (h >> 31)
}

/// Cells (XY, metres) covered by triangles that are new or gone since the
/// last build; `all` when there is no last build to compare with.
pub struct Dirty {
    all: bool,
    cells: FxHashSet<(i32, i32)>,
    lo: [f32; 2],
    hi: [f32; 2],
    pub changed: usize,
}

fn cell(v: f32) -> i32 {
    (v / CELL).floor() as i32
}

impl Dirty {
    /// Last build's answer for triangle `t` (key `key`, kept in `prev` when
    /// true) if nothing within `pad` of it changed.
    pub fn kept(&self, t: &[Vec3; 3], key: u64, pad: f32, prev: &FxHashSet<u64>) -> Option<bool> {
        (!self.touches(t[0].min(t[1]).min(t[2]), t[0].max(t[1]).max(t[2]), pad)).then(|| prev.contains(&key))
    }

    pub fn everything() -> Self {
        Self { all: true, cells: FxHashSet::default(), lo: [f32::MIN; 2], hi: [f32::MAX; 2], changed: 0 }
    }

    pub fn mark_box(&mut self, lo: Vec3, hi: Vec3) {
        self.changed += 1;
        for x in cell(lo.x)..=cell(hi.x) {
            for y in cell(lo.y)..=cell(hi.y) {
                self.cells.insert((x, y));
            }
        }
        self.lo = [self.lo[0].min(lo.x), self.lo[1].min(lo.y)];
        self.hi = [self.hi[0].max(hi.x), self.hi[1].max(hi.y)];
    }

    /// Whether the XY box `lo..hi`, widened by `pad`, meets a changed cell.
    pub fn touches(&self, lo: Vec3, hi: Vec3, pad: f32) -> bool {
        if self.all {
            return true;
        }
        if lo.x - pad > self.hi[0] + CELL || hi.x + pad < self.lo[0] - CELL || lo.y - pad > self.hi[1] + CELL || hi.y + pad < self.lo[1] - CELL {
            return false;
        }
        (cell(lo.x - pad)..=cell(hi.x + pad)).any(|x| (cell(lo.y - pad)..=cell(hi.y + pad)).any(|y| self.cells.contains(&(x, y))))
    }

    /// Whether anything at all changed (or there was nothing to compare with).
    pub fn any(&self) -> bool {
        self.all || !self.cells.is_empty()
    }

    /// Whether this compares with a last build (not built from scratch).
    pub fn compared(&self) -> bool {
        !self.all
    }
}

/// What the last area build found, by item, for `Dirty`-clean reuse.
#[derive(Default)]
pub struct AreaCache {
    /// The last build's triangles: key -> box (for the area a gone one covered).
    pub boxes: FxHashMap<u64, (Vec3, Vec3)>,
    pub rails: crate::rails::RailCache,
    pub curbs: crate::curbs::CurbCache,
    /// Triangle keys the ply filters dropped last time.
    pub interior: FxHashSet<u64>,
    pub caps: FxHashSet<u64>,
    pub buried: FxHashSet<u64>,
}

impl AreaCache {
    /// The change from the last build to `now`, recording `now` for the next,
    /// and `now`'s keys (`tri_key`).
    pub fn diff(&mut self, now: &[[Vec3; 3]]) -> (Dirty, Vec<u64>) {
        let keys: Vec<u64> = now.iter().map(tri_key).collect();
        let first = self.boxes.is_empty();
        let mut d = if first {
            Dirty::everything()
        } else {
            Dirty { all: false, cells: FxHashSet::default(), lo: [f32::MAX; 2], hi: [f32::MIN; 2], changed: 0 }
        };
        let mut boxes = FxHashMap::with_capacity_and_hasher(now.len(), Default::default());
        for (t, &k) in now.iter().zip(&keys) {
            let b = (t[0].min(t[1]).min(t[2]), t[0].max(t[1]).max(t[2]));
            if self.boxes.remove(&k).is_none() && !first {
                d.mark_box(b.0, b.1);
            }
            boxes.insert(k, b);
        }
        // What is left was in the last build and is gone now.
        for (lo, hi) in self.boxes.values() {
            d.mark_box(*lo, *hi);
        }
        self.boxes = boxes;
        (d, keys)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tri(x: f32, y: f32) -> [Vec3; 3] {
        [Vec3::new(x, y, 0.0), Vec3::new(x + 1.0, y, 0.0), Vec3::new(x, y + 1.0, 0.0)]
    }

    #[test]
    fn only_cells_near_new_or_gone_triangles_are_dirty() {
        let mut cache = AreaCache::default();
        let first = vec![tri(0.0, 0.0), tri(100.0, 0.0)];
        assert!(cache.diff(&first).0.touches(Vec3::splat(500.0), Vec3::splat(501.0), 0.0), "no last build: all dirty");
        let (same, _) = cache.diff(&first);
        assert!(!same.any(), "nothing changed");
        // One gone at x=100, one new at x=200; x=0 untouched.
        let (d, _) = cache.diff(&[tri(0.0, 0.0), tri(200.0, 0.0)]);
        assert_eq!(d.changed, 2);
        assert!(d.touches(Vec3::new(100.5, 0.5, 0.0), Vec3::new(100.5, 0.5, 0.0), 0.0));
        assert!(d.touches(Vec3::new(201.0, 0.0, 0.0), Vec3::new(201.0, 0.0, 0.0), 0.0));
        assert!(!d.touches(Vec3::new(0.0, 0.0, 0.0), Vec3::new(1.0, 1.0, 0.0), 2.0));
        assert!(d.touches(Vec3::new(90.0, 0.0, 0.0), Vec3::new(91.0, 1.0, 0.0), 10.0), "the pad reaches it");
    }
}
