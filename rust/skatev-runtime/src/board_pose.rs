//! Native drawable presentation contract. These are authored Skate joints,
//! never inferred from velocity or a host physics object.
use bevy_math::{Mat4, Quat, Vec3, Vec4};
use std::cell::Cell;

pub const NAMES: [&str; 7] = [
    "SKATEBOARD_ROOT", "TRUCK_FRONT", "TRUCK_BACK", "LEFT_WHEELFRONT",
    "LEFT_WHEELBACK", "RIGHT_WHEELFRONT", "RIGHT_WHEELBACK",
];

pub fn world(names: &[String], bones: &[Mat4], root: Mat4) -> Option<[Mat4; 7]> {
    let blender = Mat4::from_cols(Vec4::X, -Vec4::Z, Vec4::Y, Vec4::W);
    let c = Mat4::from_mat3(crate::coords::basis());
    let mut result = [Mat4::IDENTITY; 7];
    for (out, name) in result.iter_mut().zip(NAMES) {
        let index = names.iter().position(|n| n == name)?;
        *out = c * root * *bones.get(index)? * blender * c.transpose();
        if !out.is_finite() { return None; }
    }
    Some(result)
}

/// Presentation only. At riding speed Skate's wheels turn 190-280 degrees per
/// 60 Hz tick, so the drawn wheels alias (they creep backwards). Each wheel
/// keeps Skate's own spin direction and rate (unwrapped across ticks) but is
/// drawn turning at most MAX_SPIN per tick. Physics never reads this.
const MAX_SPIN: f32 = 1.0; // rad per tick; ponytail: by eye, raise if the wheels look slow
#[derive(Clone, Copy)]
struct Spin { tick: u64, raw: [f32; 4], rate: [f32; 4], shown: [f32; 4] }
thread_local!(static SPIN: Cell<Option<Spin>> = const { Cell::new(None) });

fn wrap(a: f32) -> f32 { (a + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI }

/// Wheel angle about its axle (its x axis), measured from the truck axis
/// furthest from the axle (the truck's x axis on the authored board).
fn wheel_angle(wheel: Mat4, truck: Mat4) -> (Vec3, f32) {
    let axle = wheel.x_axis.truncate().normalize_or_zero();
    let t = [truck.x_axis, truck.y_axis, truck.z_axis]
        .map(|v| v.truncate().normalize_or_zero())
        .into_iter()
        .min_by(|a, b| a.dot(axle).abs().total_cmp(&b.dot(axle).abs()))
        .unwrap();
    let reference = (t - axle * t.dot(axle)).normalize_or_zero();
    let y = wheel.y_axis.truncate();
    (axle, y.dot(axle.cross(reference)).atan2(y.dot(reference)))
}

pub fn present_wheels(world: &mut [Mat4; 7], tick: u64) {
    let measured: [(Vec3, f32); 4] = std::array::from_fn(|w| wheel_angle(world[3 + w], world[1 + w % 2]));
    let raw = measured.map(|m| m.1);
    let spin = SPIN.with(|cell| {
        let next = match cell.get() {
            Some(p) if tick == p.tick => p,
            Some(p) if tick > p.tick && tick - p.tick <= 3 => {
                let n = (tick - p.tick) as f32;
                let mut s = Spin { tick, raw, ..p };
                for w in 0..4 {
                    let d = wrap(raw[w] - p.raw[w]);
                    let expected = p.rate[w] * n;
                    let turned = [d - std::f32::consts::TAU, d, d + std::f32::consts::TAU]
                        .into_iter()
                        .min_by(|a, b| (a - expected).abs().total_cmp(&(b - expected).abs()))
                        .unwrap();
                    s.rate[w] = turned / n;
                    s.shown[w] = wrap(p.shown[w] + turned.clamp(-MAX_SPIN * n, MAX_SPIN * n));
                }
                s
            }
            _ => Spin { tick, raw, rate: [0.0; 4], shown: raw },
        };
        cell.set(Some(next));
        next
    });
    for w in 0..4 {
        let (axle, angle) = measured[w];
        let r = Quat::from_axis_angle(axle, wrap(spin.shown[w] - angle));
        let m = &mut world[3 + w];
        m.x_axis = (r * m.x_axis.truncate()).extend(0.0);
        m.y_axis = (r * m.y_axis.truncate()).extend(0.0);
        m.z_axis = (r * m.z_axis.truncate()).extend(0.0);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn native_vertex_matches_existing_skin_coordinate_chain() {
        let names = NAMES.map(str::to_string);
        let bones = std::array::from_fn::<_, 7, _>(|i| {
            Mat4::from_rotation_y(i as f32 * 0.21)
                * Mat4::from_translation(Vec3::new(i as f32 * 0.1, 0.2, -0.3))
        });
        let root = Mat4::from_translation(Vec3::new(-1300., 24., 1400.))
            * Mat4::from_rotation_y(1.2);
        let actual = world(&names, &bones, root).unwrap();
        let c = Mat4::from_mat3(crate::coords::basis());
        let blender = Mat4::from_cols(Vec4::X, -Vec4::Z, Vec4::Y, Vec4::W);
        let inverse_bind = Mat4::from_rotation_z(0.8)
            * Mat4::from_translation(Vec3::new(0.1, 0.03, -0.1));
        let vertex = Vec3::new(0.08, -0.04, 0.3);
        for i in 0..7 {
            let expected = crate::coords::from_skate(
                (root * bones[i] * blender * inverse_bind).transform_point3(vertex));
            let converted = (actual[i] * c * inverse_bind * c.transpose())
                .transform_point3(c.transform_point3(vertex));
            assert!(expected.distance(converted) < 0.0003);
        }
    }

    #[test]
    fn fast_wheels_keep_their_direction_and_slow_ones_their_angle() {
        let run = |per_tick: f32, ticks: u64| {
            SPIN.with(|c| c.set(None));
            let mut shown = 0.0;
            for t in 0..ticks {
                // Skate's rate ramps up from rest, as from a push.
                let angle = per_tick * (t * t) as f32 / (2 * ticks) as f32;
                let mut w = [Mat4::from_rotation_z(1.5707964); 7];
                for i in 3..7 { w[i] = Mat4::from_rotation_x(angle); }
                let skate = wheel_angle(w[3], w[1]).1;
                present_wheels(&mut w, 100 + t);
                shown = wrap(wheel_angle(w[3], w[1]).1 - skate);
            }
            shown
        };
        // Ending at 0.2 rad/tick: drawn exactly as Skate has it.
        let slow = run(0.2, 60);
        assert!(slow.abs() < 1e-3, "{slow}");
        // Ending at 3.3 rad/tick (aliases to -3.0): still forward, capped.
        SPIN.with(|c| c.set(None));
        let mut last = 0.0f32;
        for t in 0..120u64 {
            let rate = 3.3 * t as f32 / 119.0;
            last += rate;
            let mut w = [Mat4::IDENTITY; 7];
            for i in 3..7 { w[i] = Mat4::from_rotation_x(last); }
            present_wheels(&mut w, 500 + t);
        }
        let s = SPIN.with(|c| c.get()).unwrap();
        assert!((s.rate[0] - 3.3).abs() < 1e-3, "rate {}", s.rate[0]);
        let before = s.shown[0];
        let mut w = [Mat4::IDENTITY; 7];
        for i in 3..7 { w[i] = Mat4::from_rotation_x(last + 3.3); }
        present_wheels(&mut w, 620);
        assert!((wrap(SPIN.with(|c| c.get()).unwrap().shown[0] - before) - MAX_SPIN).abs() < 1e-3);
        // The drawn wheel stays a rotation about its own axle.
        assert!((w[3].x_axis.truncate() - Vec3::X).length() < 1e-4);
    }

    #[test]
    fn missing_or_invalid_bones_fail_closed() {
        let names = NAMES.map(str::to_string);
        let mut bones = [Mat4::IDENTITY; 7];
        assert!(world(&names[..6], &bones, Mat4::IDENTITY).is_none());
        assert!(world(&names, &bones[..6], Mat4::IDENTITY).is_none());
        bones[4].w_axis.x = f32::NAN;
        assert!(world(&names, &bones, Mat4::IDENTITY).is_none());
    }
}
