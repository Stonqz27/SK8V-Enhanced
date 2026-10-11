//! GTA ground under a point, with its collision material (materials.dat
//! order), for the host's board tracks. Each built area publishes its
//! walkable triangles; `sv_ground_at` reads the latest.
use crate::grid::{weights, XyGrid};
use bevy_math::Vec3;
use std::sync::{Arc, OnceLock, RwLock};

const CELL: f32 = 4.0;
/// Steeper than this (normal z) is a wall, not ground a wheel leaves a track on.
const MIN_UP: f32 = 0.5;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Ground {
    pub height: f32,
    pub normal: Vec3,
    pub material: u8,
}

#[derive(Default)]
pub struct GroundIndex {
    tris: Vec<([[f32; 3]; 3], u8)>,
    cells: XyGrid,
}

impl GroundIndex {
    pub fn build(tris: impl Iterator<Item = ([[f32; 3]; 3], u8)>) -> Self {
        let mut g = GroundIndex { tris: Vec::new(), cells: XyGrid::new(CELL) };
        for (v, material) in tris {
            let [a, b, c] = v.map(Vec3::from_array);
            if (b - a).cross(c - a).normalize_or_zero().z.abs() < MIN_UP {
                continue;
            }
            let lo = |i: usize| v.iter().map(|p| p[i]).fold(f32::MAX, f32::min);
            let hi = |i: usize| v.iter().map(|p| p[i]).fold(f32::MIN, f32::max);
            g.cells.insert(g.tris.len() as u32, Vec3::new(lo(0), lo(1), 0.0), Vec3::new(hi(0), hi(1), 0.0));
            g.tris.push((v, material));
        }
        g
    }

    /// The highest ground whose XY footprint holds `p`, from 0.1 m above it
    /// to `depth` below it.
    pub fn at(&self, p: Vec3, depth: f32) -> Option<Ground> {
        let mut best: Option<Ground> = None;
        for &id in self.cells.at(p.x, p.y) {
            let (v, material) = &self.tris[id as usize];
            let Some([l0, l1, l2]) = weights(v, p.x, p.y, 1e-9, 1e-4) else { continue };
            let z = l0 * v[0][2] + l1 * v[1][2] + l2 * v[2][2];
            if z > p.z + 0.1 || z < p.z - depth || best.is_some_and(|b| b.height >= z) {
                continue;
            }
            let [a, b, c] = v.map(Vec3::from_array);
            let n = (b - a).cross(c - a).normalize_or_zero();
            best = Some(Ground { height: z, normal: if n.z < 0.0 { -n } else { n }, material: *material });
        }
        best
    }
}

fn current() -> &'static RwLock<Arc<GroundIndex>> {
    static SLOT: OnceLock<RwLock<Arc<GroundIndex>>> = OnceLock::new();
    SLOT.get_or_init(|| RwLock::new(Arc::new(GroundIndex::default())))
}

pub fn publish(index: GroundIndex) {
    if let Ok(mut g) = current().write() {
        *g = Arc::new(index);
    }
}

pub fn at(p: Vec3, depth: f32) -> Option<Ground> {
    current().read().ok().and_then(|g| g.at(p, depth))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn finds_the_floor_under_a_point_and_skips_walls_and_ceilings() {
        let floor = ([[0.0, 0.0, 1.0], [10.0, 0.0, 1.0], [0.0, 10.0, 1.0]], 18u8); // SAND_LOOSE
        let roof = ([[0.0, 0.0, 5.0], [10.0, 0.0, 5.0], [0.0, 10.0, 5.0]], 4u8);
        let wall = ([[2.0, 2.0, 0.0], [2.0, 2.0, 9.0], [2.0, 3.0, 0.0]], 1u8);
        let g = GroundIndex::build([floor, roof, wall].into_iter());
        assert_eq!(g.tris.len(), 2);
        let hit = g.at(Vec3::new(2.0, 2.0, 1.05), 0.5).unwrap();
        assert_eq!(hit.material, 18);
        assert!((hit.height - 1.0).abs() < 1e-5 && (hit.normal.z - 1.0).abs() < 1e-5);
        assert!(g.at(Vec3::new(2.0, 2.0, 2.0), 0.5).is_none(), "too far above the floor");
        assert!(g.at(Vec3::new(20.0, 2.0, 1.05), 0.5).is_none(), "off the triangle");
    }
}
