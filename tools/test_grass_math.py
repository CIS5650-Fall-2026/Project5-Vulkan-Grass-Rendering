#!/usr/bin/env python3
"""Deterministic checks for the grass curve, frame, and strip mathematics.

The routines here are intentionally independent of the GLSL implementation.  They
act as a small numerical oracle for the geometric invariants that are easy to
break while changing the tessellation shaders.
"""

from __future__ import annotations

import math
import unittest
from dataclasses import dataclass
from typing import Iterable


Vec3 = tuple[float, float, float]
EPSILON = 1.0e-12


def add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(a: Vec3, scalar: float) -> Vec3:
    return (a[0] * scalar, a[1] * scalar, a[2] * scalar)


def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def length(a: Vec3) -> float:
    return math.sqrt(max(dot(a, a), 0.0))


def normalized(a: Vec3, fallback: Vec3 = (0.0, 1.0, 0.0)) -> Vec3:
    magnitude = length(a)
    return mul(a, 1.0 / magnitude) if magnitude > EPSILON else fallback


def reject(a: Vec3, normal: Vec3) -> Vec3:
    return sub(a, mul(normal, dot(a, normal)))


def finite(a: Vec3) -> bool:
    return all(math.isfinite(component) for component in a)


def rodrigues_components(
    value: Vec3, axis: Vec3, cosine: float, sine: float
) -> Vec3:
    return add(
        add(mul(value, cosine), mul(cross(axis, value), sine)),
        mul(axis, dot(axis, value) * (1.0 - cosine)),
    )


def rodrigues_angle_oracle(value: Vec3, axis: Vec3, angle: float) -> Vec3:
    cosine = math.cos(angle)
    sine = math.sin(angle)
    return add(
        add(mul(value, cosine), mul(cross(axis, value), sine)),
        mul(axis, dot(axis, value) * (1.0 - cosine)),
    )


@dataclass(frozen=True)
class Blade:
    root: Vec3
    guide: Vec3
    tip: Vec3
    up: Vec3
    root_lateral: Vec3
    width: float


def curve_point(blade: Blade, t: float) -> Vec3:
    one_minus_t = 1.0 - t
    return add(
        add(
            mul(blade.root, one_minus_t * one_minus_t),
            mul(blade.guide, 2.0 * one_minus_t * t),
        ),
        mul(blade.tip, t * t),
    )


def curve_derivative(blade: Blade, t: float) -> Vec3:
    return mul(
        add(
            mul(sub(blade.guide, blade.root), 1.0 - t),
            mul(sub(blade.tip, blade.guide), t),
        ),
        2.0,
    )


def curve_second_derivative(blade: Blade) -> Vec3:
    return mul(add(sub(blade.tip, mul(blade.guide, 2.0)), blade.root), 2.0)


def deterministic_tangent(blade: Blade, t: float) -> Vec3:
    derivative = curve_derivative(blade, t)
    if length(derivative) > EPSILON:
        return normalized(derivative)

    # A one-sided curve chord preserves the limiting tangent at collapsed end
    # handles.  An exact interior cusp has no unique tangent, so up is the
    # deterministic final choice.
    step = 1.0e-5
    if t <= step:
        chord = sub(curve_point(blade, min(t + step, 1.0)), curve_point(blade, t))
    elif t >= 1.0 - step:
        chord = sub(curve_point(blade, t), curve_point(blade, max(t - step, 0.0)))
    else:
        chord = sub(
            curve_point(blade, min(t + step, 1.0)),
            curve_point(blade, max(t - step, 0.0)),
        )
    return normalized(chord, normalized(blade.up))


def bend_basis(blade: Blade) -> tuple[Vec3, Vec3, Vec3, Vec3]:
    up = normalized(blade.up)
    lateral = normalized(reject(blade.root_lateral, up), (1.0, 0.0, 0.0))
    horizontal_tip = reject(sub(blade.tip, blade.root), up)
    if length(horizontal_tip) > EPSILON:
        bend = normalized(horizontal_tip)
    else:
        bend = normalized(cross(lateral, up), (0.0, 0.0, 1.0))
    axis = normalized(cross(up, bend), lateral)
    return up, lateral, bend, axis


def transported_frame(blade: Blade, t: float) -> tuple[Vec3, Vec3, Vec3, float]:
    up, root_lateral, bend, axis = bend_basis(blade)
    tangent = deterministic_tangent(blade, t)
    bend_component = dot(tangent, bend)
    up_component = dot(tangent, up)
    angle = math.atan2(bend_component, up_component)
    lateral = normalized(
        rodrigues_components(
            root_lateral, axis, up_component, bend_component
        ),
        root_lateral,
    )

    derivative = curve_derivative(blade, t)
    second = curve_second_derivative(blade)
    horizontal = dot(derivative, bend)
    vertical = dot(derivative, up)
    denominator = horizontal * horizontal + vertical * vertical
    if denominator > EPSILON:
        angle_rate = (
            vertical * dot(second, bend) - horizontal * dot(second, up)
        ) / denominator
    else:
        angle_rate = 0.0
    lateral_rate = mul(cross(axis, lateral), angle_rate)
    return tangent, lateral, lateral_rate, angle


def strip_point(blade: Blade, t: float, across: float) -> Vec3:
    _, lateral, _, _ = transported_frame(blade, t)
    half_width = 0.5 * blade.width * (1.0 - t)
    return add(curve_point(blade, t), mul(lateral, across * half_width))


def strip_derivatives(blade: Blade, t: float, across: float) -> tuple[Vec3, Vec3]:
    _, lateral, lateral_rate, _ = transported_frame(blade, t)
    half_root_width = 0.5 * blade.width
    half_width = half_root_width * (1.0 - t)
    width_rate = -half_root_width
    along = add(
        curve_derivative(blade, t),
        mul(
            add(mul(lateral, width_rate), mul(lateral_rate, half_width)),
            across,
        ),
    )
    across_derivative = mul(lateral, half_width)
    return along, across_derivative


def strip_normal(blade: Blade, t: float, across: float) -> Vec3:
    tangent, lateral, _, _ = transported_frame(blade, t)
    along, across_derivative = strip_derivatives(blade, t, across)
    raw = cross(across_derivative, along)
    return normalized(raw, normalized(cross(lateral, tangent)))


def rounded_raw_normal(blade: Blade, t: float, across: float) -> Vec3:
    tangent = normalized(strip_derivatives(blade, t, across)[0])
    _, raw_lateral, _, _ = transported_frame(blade, t)
    raw_normal = normalized(cross(raw_lateral, tangent), strip_normal(blade, t, across))
    center = math.sqrt(max(1.0 - 0.70 * across * across, 0.0))
    return normalized(
        add(mul(raw_normal, center), mul(raw_lateral, 0.55 * across)),
        raw_normal,
    )


def approximate_length(root: Vec3, guide: Vec3, tip: Vec3) -> float:
    return (
        2.0 * length(sub(tip, root))
        + length(sub(guide, root))
        + length(sub(tip, guide))
    ) / 3.0


def constrain_blade(
    root: Vec3, up_value: Vec3, rest_height: float, guide: Vec3, tip: Vec3
) -> tuple[Vec3, Vec3]:
    del guide
    up = normalized(up_value)
    below_ground = min(dot(up, sub(tip, root)), 0.0)
    tip = sub(tip, mul(up, below_ground))
    offset = sub(tip, root)
    horizontal_length = length(reject(offset, up))
    projected_ratio = horizontal_length / rest_height
    guide_scale = max(1.0 - projected_ratio, 0.05 * max(projected_ratio, 1.0))
    guide = add(root, mul(up, rest_height * guide_scale))
    scale = rest_height / max(approximate_length(root, guide, tip), EPSILON)
    return (
        add(root, mul(sub(guide, root), scale)),
        add(root, mul(sub(tip, root), scale)),
    )


def samples(count: int) -> Iterable[float]:
    return (index / (count - 1) for index in range(count))


class GrassMathTests(unittest.TestCase):
    def setUp(self) -> None:
        root = (0.15, -0.2, 0.35)
        up = normalized((0.08, 1.0, -0.04))
        lateral = normalized(reject((0.91, 0.12, 0.38), up))
        self.straight = Blade(
            root,
            add(root, mul(up, 0.92)),
            add(root, mul(up, 1.75)),
            up,
            lateral,
            0.18,
        )
        horizontal = normalized(reject((1.3, 0.0, 0.62), up))
        self.strong_bend = Blade(
            root,
            add(root, mul(up, 1.12)),
            add(add(root, mul(up, 0.72)), mul(horizontal, 1.34)),
            up,
            lateral,
            0.22,
        )
        # Its end derivative points below the tangent plane; the tangent passes
        # through horizontal without changing which strip edge is which.
        self.downward_tip = Blade(
            root,
            add(root, mul(up, 1.18)),
            add(add(root, mul(up, 0.18)), mul(horizontal, 1.28)),
            up,
            lateral,
            0.24,
        )

    def assert_vec_close(self, a: Vec3, b: Vec3, tolerance: float) -> None:
        self.assertLessEqual(length(sub(a, b)), tolerance, (a, b))

    def test_frames_are_finite_continuous_and_orthogonal(self) -> None:
        for blade in (self.straight, self.strong_bend, self.downward_tip):
            previous_lateral: Vec3 | None = None
            for t in samples(501):
                tangent, lateral, lateral_rate, angle = transported_frame(blade, t)
                self.assertTrue(finite(tangent) and finite(lateral) and finite(lateral_rate))
                self.assertTrue(math.isfinite(angle))
                self.assertAlmostEqual(length(tangent), 1.0, places=10)
                self.assertAlmostEqual(length(lateral), 1.0, places=10)
                self.assertAlmostEqual(dot(tangent, lateral), 0.0, places=9)
                if previous_lateral is not None:
                    self.assertGreater(dot(previous_lateral, lateral), 0.999)
                previous_lateral = lateral

    def test_component_transport_matches_angle_oracle(self) -> None:
        cusp = Blade(
            (0.0, 0.0, 0.0),
            (0.0, 1.0, 0.0),
            (0.0, 0.0, 0.0),
            (0.0, 1.0, 0.0),
            (1.0, 0.0, 0.0),
            0.1,
        )
        cases = (
            (self.strong_bend, (0.0, 0.23, 0.57, 0.91, 1.0)),
            (self.downward_tip, (0.0, 0.31, 0.68, 0.89, 1.0)),
            (cusp, (0.5,)),
        )
        for blade, parameters in cases:
            up, root_lateral, bend, axis = bend_basis(blade)
            for t in parameters:
                tangent = deterministic_tangent(blade, t)
                cosine = dot(tangent, up)
                sine = dot(tangent, bend)
                component_transport = normalized(
                    rodrigues_components(
                        root_lateral, axis, cosine, sine
                    ),
                    root_lateral,
                )
                angle_transport = normalized(
                    rodrigues_angle_oracle(
                        root_lateral, axis, math.atan2(sine, cosine)
                    ),
                    root_lateral,
                )
                _, frame_lateral, _, _ = transported_frame(blade, t)
                self.assert_vec_close(component_transport, angle_transport, 2.0e-12)
                self.assert_vec_close(frame_lateral, angle_transport, 2.0e-12)

    def test_downward_tangent_does_not_reverse_width_edges(self) -> None:
        angles: list[float] = []
        previous_edge: Vec3 | None = None
        for index in range(500):  # Exclude the truly collapsed width at t == 1.
            t = index / 500.0
            _, lateral, _, angle = transported_frame(self.downward_tip, t)
            angles.append(angle)
            edge = normalized(
                sub(
                    strip_point(self.downward_tip, t, 0.8),
                    strip_point(self.downward_tip, t, -0.8),
                )
            )
            self.assertGreater(dot(edge, lateral), 1.0 - 1.0e-10)
            if previous_edge is not None:
                self.assertGreater(dot(previous_edge, edge), 0.999)
            previous_edge = edge
        self.assertLess(min(angles), 0.5 * math.pi)
        self.assertGreater(max(angles), 0.5 * math.pi)

    def test_collapsed_handles_and_cusp_have_deterministic_fallbacks(self) -> None:
        up = (0.0, 1.0, 0.0)
        lateral = (1.0, 0.0, 0.0)
        endpoint_cases = (
            Blade((0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.0, 0.0), up, lateral, 0.1),
            Blade((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 1.0, 0.0), up, lateral, 0.1),
        )
        for blade, t in zip(endpoint_cases, (1.0, 0.0)):
            first = transported_frame(blade, t)
            second = transported_frame(blade, t)
            self.assertEqual(first, second)
            self.assertTrue(all(finite(value) for value in first[:3]))

        cusp = Blade(
            (0.0, 0.0, 0.0),
            (0.0, 1.0, 0.0),
            (0.0, 0.0, 0.0),
            up,
            lateral,
            0.1,
        )
        first = transported_frame(cusp, 0.5)
        self.assertEqual(first, transported_frame(cusp, 0.5))
        self.assertTrue(all(finite(value) for value in first[:3]))

    def test_analytic_surface_derivatives_match_finite_differences(self) -> None:
        step = 1.0e-5
        maximum_normal_error = 0.0
        for blade in (self.strong_bend, self.downward_tip):
            for t in (0.06, 0.19, 0.41, 0.63, 0.82, 0.94):
                for across in (-0.86, -0.37, 0.0, 0.44, 0.88):
                    along, across_derivative = strip_derivatives(blade, t, across)
                    finite_along = mul(
                        sub(
                            strip_point(blade, t + step, across),
                            strip_point(blade, t - step, across),
                        ),
                        0.5 / step,
                    )
                    finite_across = mul(
                        sub(
                            strip_point(blade, t, across + step),
                            strip_point(blade, t, across - step),
                        ),
                        0.5 / step,
                    )
                    self.assert_vec_close(along, finite_along, 2.0e-7)
                    self.assert_vec_close(across_derivative, finite_across, 2.0e-10)

                    analytic_normal = strip_normal(blade, t, across)
                    finite_normal = normalized(cross(finite_across, finite_along))
                    cosine = max(-1.0, min(1.0, dot(analytic_normal, finite_normal)))
                    error_degrees = math.degrees(math.acos(cosine))
                    maximum_normal_error = max(maximum_normal_error, error_degrees)
        self.assertLess(maximum_normal_error, 2.0)

    def test_front_back_sign_applies_to_entire_rounded_normal(self) -> None:
        for blade in (self.strong_bend, self.downward_tip):
            for t in (0.2, 0.55, 0.86):
                for across in (-0.9, -0.35, 0.0, 0.42, 0.9):
                    raw = rounded_raw_normal(blade, t, across)
                    front = raw
                    back = mul(raw, -1.0)
                    self.assert_vec_close(back, mul(front, -1.0), 1.0e-12)

                    opposite_across = rounded_raw_normal(blade, t, -across)
                    if abs(across) > 0.1:
                        # Across still changes the raw rounding direction.  It is
                        # not silently canceled by the face-orientation sign.
                        self.assertLess(dot(raw, opposite_across), 0.999999)

    def test_paper_constraint_restores_ground_and_approximate_length(self) -> None:
        root = (0.2, -0.3, 0.4)
        up = normalized((0.12, 1.0, -0.07))
        height = 1.65
        tips = (
            add(root, mul(up, -0.8)),
            add(add(root, mul(up, -0.35)), (1.1, 0.0, -0.4)),
            add(add(root, mul(up, 0.55)), (1.8, 0.2, 0.7)),
            add(add(root, mul(up, 1.5)), (-0.25, 0.0, 0.18)),
        )
        for tip in tips:
            guide, constrained_tip = constrain_blade(
                root, up, height, add(root, mul(up, height)), tip
            )
            self.assertGreaterEqual(dot(up, sub(constrained_tip, root)), -1.0e-10)
            self.assertLess(length(reject(sub(guide, root), up)), 1.0e-10)
            self.assertAlmostEqual(
                approximate_length(root, guide, constrained_tip), height, places=10
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
