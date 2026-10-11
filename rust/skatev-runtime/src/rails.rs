//! Grind rails found in collision: the lips a skater can grind.
//!
//! Adapted from chasmlol/2010-rust-rewrite-mashup @ ab43b8a9,
//! `crates/render_anim/src/skate/rails.rs` (Apache-2.0). The algorithm and its
//! tuned thresholds are unchanged; they are expressed in the source's map
//! units (inches). `find_metres` converts GTA metres to inches and back so no
//! constant is re-tuned for GTA. Changes from the source: glam via bevy_math,
//! the convexity gate (valley creases between bank facets are not lips),
//! buried-lip trimming (a handrail flight's top edge running on inside the
//! landing's rail is cut where it goes under), the metre wrapper, and tests.
//!
//! A lip is found by probing the geometry around each edge of a walkable face:
//! the ground must fall away just past the edge and nothing may rise there.
//! Surviving edges are merged along their lines across seams and T-junctions,
//! then chained into polylines where they meet at a gentle turn.

// Port keeps the source's inline map types verbatim.
#![allow(clippy::type_complexity)]

use rustc_hash::FxHashMap as HashMap;

use bevy_math::Vec3;

const INCH: f32 = 0.0254;

/// A face this upward is walkable; its edges are rail candidates.
const UPWARD_Z: f32 = 0.65;
/// XY grid cell for probing, in map units (inches).
const CELL: f32 = 64.0;
/// How far past the lip the probes look.
const PROBE_OUT: f32 = 2.0;
/// Open space the ground must drop below the lip, just past it.
const MIN_DROP: f32 = 3.0;
/// Height above the lip the wall probe runs at.
const WALL_PROBE_UP: f32 = 3.0;
/// Shortest rail kept, after merging and chaining.
const MIN_RAIL: f32 = 24.0;
/// Steepest rail, as rise over length: stair handrails and ramp edges pass.
const MAX_SLOPE: f32 = 0.7;
/// Chains continue through a joint turning less than about 35 degrees.
const MIN_TURN_COS: f32 = 0.82;
/// SkateV: a face across the edge rising more than about one degree above
/// the walkable face's plane makes the edge concave (sin 1 degree).
const CONCAVE_SIN: f32 = 0.017_452;
/// SkateV: lip stretches with collision this close above them are buried
/// (inside another solid) and trimmed; a board there hits that solid.
const BURIED_CLEARANCE: f32 = 18.0;
/// SkateV: spacing of the buried-lip samples along a lip.
const BURIED_STEP: f32 = 2.0;
/// SkateV: the buried test also looks this far to either side of the lip (a
/// truck's half width): a handrail tube's other top edge lies just beside it.
const BURIED_SIDE: f32 = 4.0;
/// SkateV: probes skip faces whose height span misses the probe's by more
/// than this (inches).
const HIT_Z_MARGIN: f32 = 1.0;
/// The skate engine counts rails in a u16.
const MAX_RAILS: usize = u16::MAX as usize;

#[derive(Debug, Default, Clone, Copy)]
pub struct RailCensus {
    pub candidates: usize,
    pub lips: usize,
    pub runs: usize,
    pub rails: usize,
    /// Rails dropped as duplicates of a longer rail on the same lip.
    pub duplicates: usize,
}

/// The lips each candidate edge gave last build (`reuse`), by edge identity:
/// its key and the face that defines it (the first upward face naming it).
#[derive(Default)]
pub struct RailCache {
    edges: HashMap<u64, (u32, u32)>,
    lips: Vec<(Vec3, Vec3)>,
}

/// The probes reach this far (metres) from an edge: past it, nothing a lip
/// test reads (`reuse::Dirty` pad).
const REUSE_PAD: f32 = 0.5;

/// Rails over GTA-space triangles in metres (Z up), as polylines in metres.
pub fn find_metres(tris: &[[Vec3; 3]]) -> (Vec<Vec<Vec3>>, RailCensus) {
    find_metres_reusing(tris, &crate::reuse::Dirty::everything(), &mut RailCache::default())
}

/// As `find_metres`, keeping the lips of edges `dirty` leaves untouched from
/// `cache`, which then holds this build's.
pub fn find_metres_reusing(tris: &[[Vec3; 3]], dirty: &crate::reuse::Dirty, cache: &mut RailCache) -> (Vec<Vec<Vec3>>, RailCensus) {
    let inches: Vec<[Vec3; 3]> = tris.iter().map(|t| t.map(|v| v / INCH)).collect();
    let (rails, census) = find_reusing(&inches, dirty, INCH, cache);
    (
        rails
            .into_iter()
            .map(|r| r.into_iter().map(|v| v * INCH).collect())
            .collect(),
        census,
    )
}

/// Rails over `tris` (map units, z up), each a polyline of two or more points.
pub fn find(tris: &[[Vec3; 3]]) -> (Vec<Vec<Vec3>>, RailCensus) {
    find_reusing(tris, &crate::reuse::Dirty::everything(), 1.0, &mut RailCache::default())
}

/// As `find`; `metres` converts map units to `dirty`'s metres.
fn find_reusing(tris: &[[Vec3; 3]], dirty: &crate::reuse::Dirty, metres: f32, cache: &mut RailCache) -> (Vec<Vec<Vec3>>, RailCensus) {
    let grid = Grid::build(tris);
    let mut probe = Probe {
        tris,
        grid: &grid,
        stamp: vec![0; tris.len()],
        round: 0,
        scratch: Vec::new(),
    };

    // Every edge of a walkable face, once.
    let key = |v: Vec3| v.to_array().map(|x| (x * 8.).round() as i32);
    let edge_key = |a: Vec3, b: Vec3| {
        let (ka, kb) = (key(a), key(b));
        if ka < kb { (ka, kb) } else { (kb, ka) }
    };
    // Every edge of a walkable face (the first face to name it), in key
    // order so the rails come out the same on every run.
    let mut slot: HashMap<([i32; 3], [i32; 3]), u32> = HashMap::default();
    let mut edges: Vec<(([i32; 3], [i32; 3]), (Vec3, Vec3, Vec3, Vec3))> = Vec::new();
    for tri in tris {
        let normal = (tri[1] - tri[0]).cross(tri[2] - tri[0]).normalize_or_zero();
        if normal.z <= UPWARD_Z {
            continue;
        }
        let centroid = (tri[0] + tri[1] + tri[2]) / 3.;
        for i in 0..3 {
            let (a, b) = (tri[i], tri[(i + 1) % 3]);
            let k = edge_key(a, b);
            slot.entry(k).or_insert_with(|| {
                edges.push((k, (a, b, normal, centroid)));
                edges.len() as u32 - 1
            });
        }
    }
    edges.sort_unstable_by_key(|e| e.0);
    for (i, (k, _)) in edges.iter().enumerate() {
        slot.insert(*k, i as u32);
    }
    // SkateV: every face across each candidate edge and its far vertex, for
    // the convexity gate and the lip probes below (per edge, a list threaded
    // through `fars`).
    let mut head = vec![u32::MAX; edges.len()];
    let mut fars: Vec<(Vec3, u32, u32)> = Vec::new();
    for (t, tri) in tris.iter().enumerate() {
        for i in 0..3 {
            if let Some(&e) = slot.get(&edge_key(tri[i], tri[(i + 1) % 3])) {
                fars.push((tri[(i + 2) % 3], t as u32, head[e as usize]));
                head[e as usize] = fars.len() as u32 - 1;
            }
        }
    }
    let across = |e: usize| {
        std::iter::successors(Some(head[e]).filter(|&f| f != u32::MAX), |&f| Some(fars[f as usize].2).filter(|&n| n != u32::MAX))
            .map(|f| (fars[f as usize].0, fars[f as usize].1))
    };
    let mut faces = Vec::new();
    let candidates = edges.len();

    let mut lips = Vec::new();
    let mut next = RailCache::default();
    next.edges.reserve(edges.len());
    for (e, &(k, (a, b, normal, centroid))) in edges.iter().enumerate() {
        let id = k.0.iter().chain(&k.1).map(|&x| x as u32)
            .chain([a, b, normal, centroid].iter().flat_map(|v| v.to_array()).map(f32::to_bits))
            .fold(0u64, |h, x| crate::reuse::mix(h ^ x as u64));
        let start = lips.len();
        let reused = (!dirty.touches(a.min(b) * metres, a.max(b) * metres, REUSE_PAD))
            .then(|| cache.edges.get(&id))
            .flatten()
            .map(|&(s, n)| lips.extend_from_slice(&cache.lips[s as usize..(s + n) as usize]))
            .is_some();
        'probe: {
            if reused {
                break 'probe;
            }
            let along = b - a;
            let len = along.length();
            if len < 1. || along.z.abs() > MAX_SLOPE * len {
                break 'probe;
            }
            // SkateV: a lip is a convex edge. Where a face across the edge rises
            // above this face's plane the ground climbs (a quarter-pipe or bank
            // facet crease); the probes below run along the tilted face and can
            // miss that, so it is rejected here.
            let rises = across(e).any(|(far, _)| {
                let from = far - a;
                let off_edge = from - along * (from.dot(along) / (len * len));
                off_edge.length() > 1e-3 && from.dot(normal) > CONCAVE_SIN * off_edge.length()
            });
            if rises {
                break 'probe;
            }
            let mid = (a + b) * 0.5;
            let mut out = along.cross(normal).normalize_or_zero();
            if out.dot(centroid - mid) > 0. {
                out = -out;
            }
            let samples: &[f32] = if len < 12. {
                &[0.5]
            } else {
                &[0.25, 0.5, 0.75]
            };
            faces.clear();
            faces.extend(across(e).map(|(_, t)| t));
            // Two thirds of the samples must pass: stop once that is decided.
            let need = (samples.len() * 2).div_ceil(3);
            let (mut passing, mut failing) = (0, 0);
            for &s in samples {
                if passing >= need || failing > samples.len() - need {
                    break;
                }
                if probe.is_lip(a + along * s, out, &faces) {
                    passing += 1;
                } else {
                    failing += 1;
                }
            }
            if passing >= need {
                lips.extend(probe.unburied(a, b));
            }
        }
        next.edges.insert(id, (next.lips.len() as u32, (lips.len() - start) as u32));
        next.lips.extend_from_slice(&lips[start..]);
    }
    *cache = next;
    let lip_count = lips.len();

    let runs = merge_collinear(lips);
    let run_count = runs.len();
    let mut rails = chain(runs);
    rails.retain(|rail| polyline_len(rail) >= MIN_RAIL);
    let duplicates = dedupe(&mut rails);
    if rails.len() > MAX_RAILS {
        rails.sort_by(|a, b| polyline_len(b).total_cmp(&polyline_len(a)));
        rails.truncate(MAX_RAILS);
    }
    let census = RailCensus {
        candidates,
        lips: lip_count,
        runs: run_count,
        rails: rails.len(),
        duplicates,
    };
    (rails, census)
}

/// SkateV: a rail lying wholly within a wheel's diameter of a longer rail is
/// the same lip to a skater (stacked GTA faces, a deck skin over its ramp,
/// yield lips one to three centimetres apart; one wheel spans both, the grind
/// probes cannot tell them apart and the duplicates multiply the catches
/// along that lip). The longer rail is kept. Returns how many were dropped.
fn dedupe(rails: &mut Vec<Vec<Vec3>>) -> usize {
    let radius = 2.0 * skate_core::physics::board::RETAIL_WHEEL_RADIUS / INCH;
    let mut order: Vec<usize> = (0..rails.len()).collect();
    order.sort_by(|&a, &b| polyline_len(&rails[b]).total_cmp(&polyline_len(&rails[a])));
    // Kept segments by grid cell (every cell a segment's padded box touches).
    let mut cells: HashMap<(i32, i32), Vec<(Vec3, Vec3)>> = HashMap::default();
    let mut keep = vec![false; rails.len()];
    for i in order {
        let rail = &rails[i];
        let near = |p: Vec3| {
            cells.get(&(cell_of(p.x), cell_of(p.y))).is_some_and(|segs| {
                segs.iter().any(|&(a, b)| {
                    let ab = b - a;
                    let t = ((p - a).dot(ab) / ab.length_squared().max(1e-12)).clamp(0., 1.);
                    p.distance(a + ab * t) <= radius
                })
            })
        };
        let covered = rail.windows(2).all(|w| {
            let steps = (w[0].distance(w[1]) / radius).ceil().max(1.) as usize;
            (0..=steps).all(|k| near(w[0].lerp(w[1], k as f32 / steps as f32)))
        });
        if covered {
            continue;
        }
        keep[i] = true;
        for w in rail.windows(2) {
            let (lo, hi) = (w[0].min(w[1]) - Vec3::splat(radius), w[0].max(w[1]) + Vec3::splat(radius));
            for x in cell_of(lo.x)..=cell_of(hi.x) {
                for y in cell_of(lo.y)..=cell_of(hi.y) {
                    cells.entry((x, y)).or_default().push((w[0], w[1]));
                }
            }
        }
    }
    let before = rails.len();
    let mut k = keep.into_iter();
    rails.retain(|_| k.next().unwrap_or(true));
    before - rails.len()
}

fn polyline_len(points: &[Vec3]) -> f32 {
    points.windows(2).map(|w| w[0].distance(w[1])).sum()
}

struct Grid {
    xy: crate::grid::XyGrid,
    /// Each triangle's height span (lowest, highest vertex z).
    span: Vec<(f32, f32)>,
}

fn cell_of(v: f32) -> i32 {
    (v / CELL).floor() as i32
}

impl Grid {
    fn build(tris: &[[Vec3; 3]]) -> Self {
        let span = tris
            .iter()
            .map(|t| (t[0].z.min(t[1].z).min(t[2].z), t[0].z.max(t[1].z).max(t[2].z)))
            .collect();
        Self { xy: crate::grid::XyGrid::triangles(CELL, tris), span }
    }
}

struct Probe<'a> {
    tris: &'a [[Vec3; 3]],
    grid: &'a Grid,
    stamp: Vec<u32>,
    round: u32,
    scratch: Vec<u32>,
}

impl Probe<'_> {
    /// Past `p` along `out` the ground falls away and nothing rises. The
    /// faces `across` the edge are tried first: on open ground one of them
    /// takes the drop probe, and the grid would find it too.
    fn is_lip(&mut self, p: Vec3, out: Vec3, across: &[u32]) -> bool {
        let down_from = p + out * PROBE_OUT + Vec3::Z;
        let tris = self.tris;
        if across.iter().any(|&i| ray_triangle(down_from, -Vec3::Z, 1. + MIN_DROP, &tris[i as usize]))
            || self.hit(down_from, -Vec3::Z, 1. + MIN_DROP)
        {
            return false;
        }
        let across_from = p - out + Vec3::Z * WALL_PROBE_UP;
        !self.hit(across_from, out, 2. + PROBE_OUT)
    }

    /// SkateV: the triangles any buried-lip ray of lip `a`..`b` could hit:
    /// those whose box meets the prism the rays sweep (the lip padded by the
    /// truck half width, from just above it to the clearance). Testing the
    /// rays against this short list gives the same answers as probing the
    /// grid's full-height columns per sample (2-3 s per area:
    /// collision fell behind the skater).
    fn burial_candidates(&mut self, a: Vec3, b: Vec3) -> Vec<u32> {
        // Inch coordinates run to ~1e5 (float steps ~0.01): pad generously.
        let pad = Vec3::new(BURIED_SIDE, BURIED_SIDE, 0.) + Vec3::splat(0.25);
        let lo = a.min(b) - pad + Vec3::Z * 0.5;
        let hi = a.max(b) + pad + Vec3::Z * (0.5 + BURIED_CLEARANCE);
        self.round = self.round.wrapping_add(1);
        if self.round == 0 {
            self.stamp.fill(0);
            self.round = 1;
        }
        let mut out = Vec::new();
        for x in cell_of(lo.x)..=cell_of(hi.x) {
            for y in cell_of(lo.y)..=cell_of(hi.y) {
                for &index in self.grid.xy.cell(x, y) {
                    let (zl, zh) = self.grid.span[index as usize];
                    if zh < lo.z || zl > hi.z {
                        continue;
                    }
                    let seen = &mut self.stamp[index as usize];
                    if *seen == self.round {
                        continue;
                    }
                    *seen = self.round;
                    let t = &self.tris[index as usize];
                    let (tlo, thi) = (t[0].min(t[1]).min(t[2]), t[0].max(t[1]).max(t[2]));
                    if tlo.cmple(hi).all() && thi.cmpge(lo).all() {
                        out.push(index);
                    }
                }
            }
        }
        out
    }

    /// SkateV: the stretches of lip `a`..`b` that are not buried, with each
    /// cut refined to a fraction of an inch. A point is buried when solid
    /// geometry lies just above it or a truck's half width to either side
    /// (a stair handrail's sloped top edge continuing under the landing
    /// rail's top: bails at the bottom or half-way
    /// point of a handrail).
    fn unburied(&mut self, a: Vec3, b: Vec3) -> Vec<(Vec3, Vec3)> {
        let candidates = self.burial_candidates(a, b);
        if candidates.is_empty() {
            return vec![(a, b)];
        }
        let tris = self.tris;
        let steps = ((b - a).length() / BURIED_STEP).ceil().max(1.) as usize;
        let side = Vec3::new(a.y - b.y, b.x - a.x, 0.).normalize_or_zero() * BURIED_SIDE;
        let offsets = [Vec3::ZERO, side, -side];
        // Per candidate and ray line (the lip, and the lip moved a truck's
        // half width to either side): the stretch of lip fractions over the
        // triangle's plan, padded a quarter inch. A vertical ray outside it
        // cannot meet the triangle; one inside is tested as before. Faces
        // the vertical rays cannot hit (Möller–Trumbore's determinant, as
        // `ray_triangle` computes it) are dropped.
        let d = b - a;
        let pad = 0.25 / Vec3::new(d.x, d.y, 0.).length().max(1e-3);
        let mut spans: Vec<(u32, [(f32, f32); 3])> = Vec::with_capacity(candidates.len());
        for &i in &candidates {
            let t = &tris[i as usize];
            let (e1, e2) = (t[1] - t[0], t[2] - t[0]);
            if e1.dot(Vec3::Z.cross(e2)).abs() < 1e-8 {
                continue;
            }
            let lines = offsets.map(|off| plan_span(a + off, d, t, pad));
            if lines.iter().any(|(t0, t1)| t0 <= t1) {
                spans.push((i, lines));
            }
        }
        if spans.is_empty() {
            return vec![(a, b)];
        }
        let buried = |p: Vec3, f: f32| {
            offsets.iter().enumerate().any(|(k, &off)| {
                let origin = p + off + Vec3::Z * 0.5;
                // Faces wholly below or above the ray are skipped: nearly
                // parallel to it (a wall face ending at the lip) Möller–Trumbore
                // can report them hit.
                spans.iter().any(|(i, lines)| {
                    let (t0, t1) = lines[k];
                    let t = &tris[*i as usize];
                    f >= t0
                        && f <= t1
                        && t[0].z.max(t[1].z).max(t[2].z) >= origin.z
                        && t[0].z.min(t[1].z).min(t[2].z) <= origin.z + BURIED_CLEARANCE
                        && ray_triangle(origin, Vec3::Z, BURIED_CLEARANCE, t)
                })
            })
        };
        let at = |i: usize| i as f32 / steps as f32;
        let clear: Vec<bool> = (0..=steps).map(|i| !buried(a.lerp(b, at(i)), at(i))).collect();
        if clear.iter().all(|c| *c) {
            return vec![(a, b)];
        }
        // The last clear fraction between a clear sample and a buried one.
        let edge = |mut inside: f32, mut outside: f32| {
            for _ in 0..8 {
                let mid = (inside + outside) * 0.5;
                if buried(a.lerp(b, mid), mid) {
                    outside = mid;
                } else {
                    inside = mid;
                }
            }
            inside
        };
        let mut spans = Vec::new();
        let mut i = 0;
        while i <= steps {
            if !clear[i] {
                i += 1;
                continue;
            }
            let first = i;
            while i < steps && clear[i + 1] {
                i += 1;
            }
            let t0 = if first == 0 { 0. } else { edge(at(first), at(first - 1)) };
            let t1 = if i == steps { 1. } else { edge(at(i), at(i + 1)) };
            if (t1 - t0) * (b - a).length() >= 1. {
                spans.push((a.lerp(b, t0), a.lerp(b, t1)));
            }
            i += 1;
        }
        spans
    }

    /// Whether the segment from `origin` along unit `dir` for `len` crosses
    /// any triangle, either side facing.
    fn hit(&mut self, origin: Vec3, dir: Vec3, len: f32) -> bool {
        let end = origin + dir * len;
        let (min, max) = (origin.min(end), origin.max(end));
        let (zlo, zhi) = (min.z - HIT_Z_MARGIN, max.z + HIT_Z_MARGIN);
        self.round = self.round.wrapping_add(1);
        if self.round == 0 {
            self.stamp.fill(0);
            self.round = 1;
        }
        self.scratch.clear();
        for x in cell_of(min.x)..=cell_of(max.x) {
            for y in cell_of(min.y)..=cell_of(max.y) {
                for &index in self.grid.xy.cell(x, y) {
                    let (lo, hi) = self.grid.span[index as usize];
                    if hi < zlo || lo > zhi {
                        continue;
                    }
                    let seen = &mut self.stamp[index as usize];
                    if *seen != self.round {
                        *seen = self.round;
                        self.scratch.push(index);
                    }
                }
            }
        }
        self.scratch
            .iter()
            .any(|&index| ray_triangle(origin, dir, len, &self.tris[index as usize]))
    }
}

/// The fractions `t` in 0..=1 where point `from + along * t` lies over
/// triangle `tri` in plan (XY), widened by `pad`; empty (t0 > t1) if none.
fn plan_span(from: Vec3, along: Vec3, tri: &[Vec3; 3], pad: f32) -> (f32, f32) {
    let (mut t0, mut t1) = (0f32, 1f32);
    // Counterclockwise in plan: inside is left of every edge.
    let ccw = (tri[1].x - tri[0].x) * (tri[2].y - tri[0].y) - (tri[1].y - tri[0].y) * (tri[2].x - tri[0].x) > 0.;
    for i in 0..3 {
        let (p, q) = if ccw { (tri[i], tri[(i + 1) % 3]) } else { (tri[(i + 1) % 3], tri[i]) };
        let (ex, ey) = (q.x - p.x, q.y - p.y);
        let len = (ex * ex + ey * ey).sqrt();
        if len < 1e-9 {
            continue;
        }
        // Signed distance left of the edge, linear in t: s0 + ds * t >= -pad_dist.
        let s0 = (ex * (from.y - p.y) - ey * (from.x - p.x)) / len;
        let ds = (ex * along.y - ey * along.x) / len;
        let slack = s0 + pad * (along.x * along.x + along.y * along.y).sqrt() + 0.25;
        if ds.abs() < 1e-12 {
            if slack < 0. {
                return (1., 0.);
            }
        } else if ds > 0. {
            t0 = t0.max(-slack / ds);
        } else {
            t1 = t1.min(-slack / ds);
        }
    }
    (t0 - pad, t1 + pad)
}

/// Möller–Trumbore, both faces, hits within `(0, len]`.
fn ray_triangle(origin: Vec3, dir: Vec3, len: f32, tri: &[Vec3; 3]) -> bool {
    let e1 = tri[1] - tri[0];
    let e2 = tri[2] - tri[0];
    let p = dir.cross(e2);
    let det = e1.dot(p);
    if det.abs() < 1e-8 {
        return false;
    }
    let inv = 1. / det;
    let s = origin - tri[0];
    let u = s.dot(p) * inv;
    if !(0. ..=1.).contains(&u) {
        return false;
    }
    let q = s.cross(e1);
    let v = dir.dot(q) * inv;
    if v < 0. || u + v > 1. {
        return false;
    }
    let t = e2.dot(q) * inv;
    t > 1e-4 && t <= len
}

/// Lips on one line, merged into maximal runs where they overlap or touch.
fn merge_collinear(lips: Vec<(Vec3, Vec3)>) -> Vec<(Vec3, Vec3)> {
    let mut lines: HashMap<([i32; 3], [i32; 3]), (Vec3, Vec3, Vec<(f32, f32)>)> = HashMap::default();
    for (mut a, mut b) in lips {
        let mut d = (b - a).normalize_or_zero();
        if d == Vec3::ZERO {
            continue;
        }
        let flip =
            d.x < -1e-4 || (d.x.abs() <= 1e-4 && (d.y < -1e-4 || (d.y.abs() <= 1e-4 && d.z < 0.)));
        if flip {
            d = -d;
            std::mem::swap(&mut a, &mut b);
        }
        let o = a - d * a.dot(d);
        let k = (
            (d * 64.).to_array().map(|x| x.round() as i32),
            (o * 2.).to_array().map(|x| x.round() as i32),
        );
        let line = lines.entry(k).or_insert((d, o, Vec::new()));
        line.2.push((a.dot(line.0), b.dot(line.0)));
    }
    let mut runs = Vec::new();
    let mut lines: Vec<_> = lines.into_iter().collect();
    lines.sort_unstable_by_key(|(k, _)| *k);
    for (_, (d, o, mut spans)) in lines {
        spans.sort_by(|x, y| x.0.total_cmp(&y.0));
        let mut current = spans[0];
        for &(t0, t1) in &spans[1..] {
            if t0 <= current.1 + 1.5 {
                current.1 = current.1.max(t1);
            } else {
                runs.push((o + d * current.0, o + d * current.1));
                current = (t0, t1);
            }
        }
        runs.push((o + d * current.0, o + d * current.1));
    }
    runs
}

/// Runs joined end to end into polylines through joints where exactly two
/// runs meet at a gentle turn.
fn chain(runs: Vec<(Vec3, Vec3)>) -> Vec<Vec<Vec3>> {
    let node = |v: Vec3| v.to_array().map(|x| x.round() as i32);
    let mut at: HashMap<[i32; 3], Vec<(usize, bool)>> = HashMap::default();
    for (i, (a, b)) in runs.iter().enumerate() {
        at.entry(node(*a)).or_default().push((i, false));
        at.entry(node(*b)).or_default().push((i, true));
    }
    let mut used = vec![false; runs.len()];
    let far = |i: usize, from_end: bool| if from_end { runs[i].0 } else { runs[i].1 };
    let near = |i: usize, from_end: bool| if from_end { runs[i].1 } else { runs[i].0 };
    // The run continuing a polyline at `joint`, arriving along `heading`.
    let next = |joint: Vec3, heading: Vec3, used: &[bool]| -> Option<(usize, bool)> {
        let there = at.get(&node(joint))?;
        if there.len() != 2 {
            return None;
        }
        let &(i, at_end) = there.iter().find(|(i, _)| !used[*i])?;
        let leave = (far(i, at_end) - near(i, at_end)).normalize_or_zero();
        (heading.dot(leave) >= MIN_TURN_COS).then_some((i, at_end))
    };
    let mut rails = Vec::new();
    for start in 0..runs.len() {
        if used[start] {
            continue;
        }
        used[start] = true;
        let (a, b) = runs[start];
        let mut points = std::collections::VecDeque::from([a, b]);
        // Forward from b, then backward from a.
        let mut tip = b;
        let mut heading = (b - a).normalize_or_zero();
        while let Some((i, at_end)) = next(tip, heading, &used) {
            used[i] = true;
            let to = far(i, at_end);
            heading = (to - tip).normalize_or_zero();
            tip = to;
            points.push_back(to);
        }
        let mut tip = a;
        let mut heading = (a - b).normalize_or_zero();
        while let Some((i, at_end)) = next(tip, heading, &used) {
            used[i] = true;
            let to = far(i, at_end);
            heading = (to - tip).normalize_or_zero();
            tip = to;
            points.push_front(to);
        }
        rails.push(points.into_iter().collect());
    }
    rails
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Two counterclockwise (upward) triangles covering an axis-aligned quad.
    fn quad(min: Vec3, max: Vec3, z: f32, out: &mut Vec<[Vec3; 3]>) {
        let (a, b) = (Vec3::new(min.x, min.y, z), Vec3::new(max.x, min.y, z));
        let (c, d) = (Vec3::new(max.x, max.y, z), Vec3::new(min.x, max.y, z));
        out.push([a, b, c]);
        out.push([a, c, d]);
    }

    /// A vertical wall face along y = `y` from x0..x1, z0..z1.
    fn wall_y(y: f32, x0: f32, x1: f32, z0: f32, z1: f32, out: &mut Vec<[Vec3; 3]>) {
        let (a, b) = (Vec3::new(x0, y, z0), Vec3::new(x1, y, z0));
        let (c, d) = (Vec3::new(x1, y, z1), Vec3::new(x0, y, z1));
        out.push([a, b, c]);
        out.push([a, c, d]);
    }

    /// A 0.4 m high ledge, 3 m long, 0.5 m deep, standing on open ground.
    fn ledge() -> Vec<[Vec3; 3]> {
        let mut t = Vec::new();
        quad(
            Vec3::new(-10.0, -10.0, 0.0),
            Vec3::new(10.0, 0.0, 0.0),
            0.0,
            &mut t,
        );
        quad(
            Vec3::new(-10.0, 0.5, 0.0),
            Vec3::new(10.0, 10.0, 0.0),
            0.0,
            &mut t,
        );
        quad(
            Vec3::new(-1.5, 0.0, 0.0),
            Vec3::new(1.5, 0.5, 0.0),
            0.4,
            &mut t,
        );
        wall_y(0.0, -1.5, 1.5, 0.0, 0.4, &mut t);
        wall_y(0.5, -1.5, 1.5, 0.0, 0.4, &mut t);
        t
    }

    #[test]
    fn rails_on_the_same_lip_collapse_to_the_longest() {
        let r = skate_core::physics::board::RETAIL_WHEEL_RADIUS / INCH;
        let line = |y: f32, x0: f32, x1: f32| vec![Vec3::new(x0, y, 10.), Vec3::new(x1, y, 10.)];
        // A lip, a shorter copy just over a wheel radius off it, and a
        // parallel lip three wheel radii away (a separate edge).
        let mut rails = vec![line(1.1 * r, 5., 95.), line(0., 0., 100.), line(3. * r, 0., 100.)];
        assert_eq!(dedupe(&mut rails), 1);
        assert_eq!(rails, vec![line(0., 0., 100.), line(3. * r, 0., 100.)]);
        // A copy only partly on the lip stays.
        let mut rails = vec![line(0., 0., 100.), line(r / 3., 50., 150.)];
        assert_eq!(dedupe(&mut rails), 0);
    }

    #[test]
    fn ledge_lips_become_rails_in_metres() {
        let (rails, census) = find_metres(&ledge());
        assert!(census.rails >= 2, "{census:?}");
        let on_top = |r: &&Vec<Vec3>| r.iter().all(|p| (p.z - 0.4).abs() < 1e-3);
        let lips: Vec<_> = rails
            .iter()
            .filter(on_top)
            .filter(|r| polyline_len(r) > 2.9)
            .collect();
        assert!(lips.len() >= 2, "front and back ledge lips: {rails:?}");
        for y in [0.0, 0.5] {
            assert!(
                lips.iter()
                    .any(|r| r.iter().all(|p| (p.y - y).abs() < 1e-3)),
                "lip along y={y}: {lips:?}"
            );
        }
        // Where the ground meets the ledge wall nothing drops away.
        let seam = rails.iter().any(|r| {
            r.iter().all(|p| {
                p.z.abs() < 1e-3
                    && p.x.abs() < 1.6
                    && (p.y.abs() < 1e-3 || (p.y - 0.5).abs() < 1e-3)
            })
        });
        assert!(!seam, "ground/ledge seam must not be a rail: {rails:?}");
    }

    #[test]
    fn flat_ground_interior_edges_are_not_rails() {
        let mut t = Vec::new();
        for i in -3..3 {
            for j in -3..3 {
                let min = Vec3::new(i as f32 * 2.0, j as f32 * 2.0, 0.0);
                quad(min, min + Vec3::new(2.0, 2.0, 0.0), 0.0, &mut t);
            }
        }
        // Surround the plaza with ground so only interior seams are candidates.
        quad(
            Vec3::new(-50.0, -50.0, 0.0),
            Vec3::new(50.0, -6.0, 0.0),
            0.0,
            &mut t,
        );
        quad(
            Vec3::new(-50.0, 6.0, 0.0),
            Vec3::new(50.0, 50.0, 0.0),
            0.0,
            &mut t,
        );
        quad(
            Vec3::new(-50.0, -6.0, 0.0),
            Vec3::new(-6.0, 6.0, 0.0),
            0.0,
            &mut t,
        );
        quad(
            Vec3::new(6.0, -6.0, 0.0),
            Vec3::new(50.0, 6.0, 0.0),
            0.0,
            &mut t,
        );
        let (rails, census) = find_metres(&t);
        let interior = rails
            .iter()
            .any(|r| r.iter().all(|p| p.x.abs() < 5.9 && p.y.abs() < 5.9));
        assert!(!interior, "{census:?} {rails:?}");
    }

    #[test]
    fn edge_against_a_rising_wall_is_not_a_lip() {
        let mut t = Vec::new();
        quad(
            Vec3::new(-2.0, -2.0, 0.0),
            Vec3::new(2.0, 0.0, 0.0),
            0.0,
            &mut t,
        );
        wall_y(0.0, -2.0, 2.0, 0.0, 3.0, &mut t);
        let (rails, _) = find_metres(&t);
        assert!(
            !rails
                .iter()
                .any(|r| r.iter().all(|p| p.y.abs() < 1e-3 && p.z.abs() < 1e-3)),
            "floor/wall seam must not be grindable: {rails:?}"
        );
    }

    #[test]
    fn bank_facet_crease_is_not_a_lip() {
        // Floor, a 30 degree facet, then a steeper 57 degree facet: the valley
        // between the two facets rises on both sides and is never grindable.
        let p = |x: f32, y: f32, z: f32| Vec3::new(x, y, z);
        let (z1, z2) = (0.5 * 30f32.to_radians().tan(), 0.5 * 30f32.to_radians().tan() + 0.5 * 57f32.to_radians().tan());
        let mut t = Vec::new();
        quad(p(-3., -3., 0.), p(3., 0., 0.), 0., &mut t);
        for (y0, y1, za, zb) in [(0., 0.5, 0., z1), (0.5, 1.0, z1, z2)] {
            t.push([p(-3., y0, za), p(3., y0, za), p(3., y1, zb)]);
            t.push([p(-3., y0, za), p(3., y1, zb), p(-3., y1, zb)]);
        }
        let (rails, _) = find_metres(&t);
        assert!(
            !rails.iter().any(|r| r.iter().all(|v| (v.y - 0.5).abs() < 1e-3)),
            "facet crease must not be a rail: {rails:?}"
        );
    }

    /// A box (all six faces) from `min` to `max`, tilted so its top rises by
    /// `rise` from min.x to max.x.
    fn sloped_box(min: Vec3, max: Vec3, rise: f32, out: &mut Vec<[Vec3; 3]>) {
        let p = |x: f32, y: f32, z: f32| Vec3::new(x, y, z + rise * (x - min.x) / (max.x - min.x));
        let c = [
            p(min.x, min.y, min.z), p(max.x, min.y, min.z), p(max.x, max.y, min.z), p(min.x, max.y, min.z),
            p(min.x, min.y, max.z), p(max.x, min.y, max.z), p(max.x, max.y, max.z), p(min.x, max.y, max.z),
        ];
        for [a, b, cc, d] in [[4, 5, 6, 7], [3, 2, 1, 0], [0, 1, 5, 4], [2, 3, 7, 6], [1, 2, 6, 5], [3, 0, 4, 7]] {
            out.push([c[a], c[b], c[cc]]);
            out.push([c[a], c[cc], c[d]]);
        }
    }

    #[test]
    fn handrail_flight_is_cut_where_it_runs_under_the_landing_rail() {
        // A 1 m high landing rail from x 0..2 (top 1.0) and a flight rail
        // coming down from x -3 that runs on 0.4 m into the landing rail,
        // its top edge going 0.24 m under the landing rail's top.
        let mut t = Vec::new();
        quad(Vec3::new(-10.0, -10.0, -5.0), Vec3::new(10.0, 10.0, -5.0), -5.0, &mut t);
        sloped_box(Vec3::new(0.0, 0.0, 0.9), Vec3::new(2.0, 0.3, 1.0), 0.0, &mut t);
        // Top from 3.0 at x -3 down to 1.0 at x 0 (slope -2/3), on to x 0.4.
        sloped_box(Vec3::new(-3.0, 0.0, 2.9), Vec3::new(0.4, 0.3, 3.0), -2.0 * 3.4 / 3.0, &mut t);
        let (rails, _) = find_metres(&t);
        let flight: Vec<_> = rails
            .iter()
            .filter(|r| r.iter().any(|p| p.x < -2.5 && p.z > 2.5))
            .collect();
        assert!(!flight.is_empty(), "the flight's lip is a rail: {rails:?}");
        for r in &flight {
            let end = r.iter().map(|p| p.x).fold(f32::MIN, f32::max);
            assert!(end < 0.02, "flight rail must stop where it goes under the landing rail, ends at x {end}: {r:?}");
            assert!(end > -0.05, "flight rail keeps its open part, ends at x {end}");
        }
        // The landing rail itself is untouched.
        assert!(rails.iter().any(|r| r.iter().all(|p| (p.z - 1.0).abs() < 1e-3) && polyline_len(r) > 1.9), "{rails:?}");
    }

    #[test]
    fn non_walkable_faces_are_not_candidates() {
        let mut t = Vec::new();
        wall_y(0.0, -2.0, 2.0, 0.0, 1.0, &mut t);
        let (_, census) = find_metres(&t);
        assert_eq!(census.candidates, 0);
    }

    #[test]
    fn short_lips_are_dropped() {
        let mut t = Vec::new();
        quad(
            Vec3::new(0.0, 0.0, 0.0),
            Vec3::new(0.3, 0.3, 0.0),
            1.0,
            &mut t,
        );
        let (rails, _) = find_metres(&t);
        assert!(
            rails
                .iter()
                .all(|r| polyline_len(r) >= MIN_RAIL * INCH - 1e-4)
        );
        assert!(
            rails.is_empty(),
            "0.3 m edges are under the 24 in minimum: {rails:?}"
        );
    }
}
