"""
Dynamic Generative Particle Artist for SphereAI.
Can procedurally construct 3D particle coordinate representations for ANY subject,
semantic category, or arbitrary LLM-generated vector strokes.
"""

import math
import re
import random

NUM_PARTICLES = 600

def _line(p1, p2, steps=10):
    pts = []
    for i in range(steps):
        t = i / float(steps - 1) if steps > 1 else 0.0
        pts.append([
            p1[0] + (p2[0] - p1[0]) * t,
            p1[1] + (p2[1] - p1[1]) * t,
            (p1[2] if len(p1) > 2 else 0.0) + ((p2[2] if len(p2) > 2 else 0.0) - (p1[2] if len(p1) > 2 else 0.0)) * t
        ])
    return pts

def _circle(cx, cy, r, steps=36, z=0.0):
    pts = []
    for i in range(steps):
        a = (i / float(steps)) * 2.0 * math.pi
        pts.append([cx + math.cos(a) * r, cy + math.sin(a) * r, z])
    pts.append(pts[0])
    return pts

def _arc(cx, cy, r, a1, a2, steps=24, z=0.0):
    pts = []
    for i in range(steps):
        a = a1 + (i / float(steps - 1)) * (a2 - a1)
        pts.append([cx + math.cos(a) * r, cy + math.sin(a) * r, z])
    return pts

def _ellipse(cx, cy, rx, ry, steps=36, z=0.0):
    pts = []
    for i in range(steps):
        a = (i / float(steps)) * 2.0 * math.pi
        pts.append([cx + math.cos(a) * rx, cy + math.sin(a) * ry, z])
    pts.append(pts[0])
    return pts

def sample_strokes_to_particles(strokes, n=NUM_PARTICLES):
    """Uniformly samples n particles across all line segments in the strokes."""
    segs = []
    total_len = 0.0

    for stroke in strokes:
        if not stroke or len(stroke) < 2:
            continue
        for i in range(len(stroke) - 1):
            p1 = stroke[i]
            p2 = stroke[i+1]
            z1 = p1[2] if len(p1) > 2 else 0.0
            z2 = p2[2] if len(p2) > 2 else 0.0
            dx = p2[0] - p1[0]
            dy = p2[1] - p1[1]
            dz = z2 - z1
            l = math.sqrt(dx * dx + dy * dy + dz * dz)
            if l > 0.0001:
                segs.append(([p1[0], p1[1], z1], [p2[0], p2[1], z2], l))
                total_len += l

    coords = []
    if total_len == 0.0 or not segs:
        # Fallback to golden spiral sphere
        golden_angle = math.pi * (3.0 - math.sqrt(5.0))
        for i in range(n):
            y = 1.0 - (i / float(n - 1)) * 2.0
            radius = math.sqrt(max(0.0, 1.0 - y * y))
            theta = golden_angle * i
            coords.append([round(math.cos(theta) * radius, 4), round(y, 4), round(math.sin(theta) * radius, 4)])
        return coords

    for i in range(n):
        target = (i / float(n)) * total_len
        accum = 0.0
        placed = False
        for p1, p2, l in segs:
            if accum + l >= target:
                t = (target - accum) / l
                x = p1[0] + (p2[0] - p1[0]) * t
                y = p1[1] + (p2[1] - p1[1]) * t
                z = p1[2] + (p2[2] - p1[2]) * t
                coords.append([round(x, 4), round(y, 4), round(z, 4)])
                placed = True
                break
            accum += l
        if not placed:
            coords.append([round(segs[-1][1][0], 4), round(segs[-1][1][1], 4), round(segs[-1][1][2], 4)])

    return coords

# -------------------------------------------------------------
# Rich Semantic Geometric Primitives for Common Subjects
# -------------------------------------------------------------

def build_rocket():
    strokes = [
        # Fuselage & nosecone
        _line([0.0, -0.75], [-0.18, -0.40], 12) + _line([-0.18, -0.40], [-0.18, 0.35], 16) + _line([-0.18, 0.35], [0.18, 0.35], 10) + _line([0.18, 0.35], [0.18, -0.40], 16) + _line([0.18, -0.40], [0.0, -0.75], 12),
        # Porthole window
        _circle(0.0, -0.15, 0.09, 24),
        # Left wing fin
        _line([-0.18, 0.10], [-0.46, 0.45], 12) + _line([-0.46, 0.45], [-0.18, 0.35], 10),
        # Right wing fin
        _line([0.18, 0.10], [0.46, 0.45], 12) + _line([0.46, 0.45], [0.18, 0.35], 10),
        # Center booster nozzle
        _line([-0.10, 0.35], [-0.12, 0.45], 6) + _line([-0.12, 0.45], [0.12, 0.45], 8) + _line([0.12, 0.45], [0.10, 0.35], 6),
        # Thrust flame tongues
        _line([-0.10, 0.45], [-0.05, 0.70], 10) + _line([-0.05, 0.70], [0.0, 0.52], 8) + _line([0.0, 0.52], [0.05, 0.70], 10) + _line([0.05, 0.70], [0.10, 0.45], 10)
    ]
    return strokes, "cyan"

def build_airplane():
    strokes = [
        # Fuselage
        _ellipse(0.0, -0.05, 0.08, 0.65, 36),
        # Main swept wings
        _line([-0.75, 0.08], [0.0, -0.15], 24) + _line([0.0, -0.15], [0.75, 0.08], 24),
        _line([-0.75, 0.08], [-0.70, 0.18], 6) + _line([-0.70, 0.18], [-0.08, 0.15], 18),
        _line([0.75, 0.08], [0.70, 0.18], 6) + _line([0.70, 0.18], [0.08, 0.15], 18),
        # Tail horizontal stabilizers
        _line([-0.30, 0.52], [0.0, 0.48], 12) + _line([0.0, 0.48], [0.30, 0.52], 12),
        # Cockpit window
        _arc(0.0, -0.45, 0.06, 0.0, math.pi, 12)
    ]
    return strokes, "cyan"

def build_sword():
    strokes = [
        # Blade point & edges
        _line([0.0, -0.75], [-0.08, -0.60], 8) + _line([-0.08, -0.60], [-0.08, 0.22], 24) + _line([-0.08, 0.22], [0.08, 0.22], 8) + _line([0.08, 0.22], [0.08, -0.60], 24) + _line([0.08, -0.60], [0.0, -0.75], 8),
        # Central fuller spine
        _line([0.0, -0.68], [0.0, 0.20], 30),
        # Crossguard
        _line([-0.36, 0.22], [0.36, 0.22], 20) + _line([0.36, 0.22], [0.36, 0.26], 4) + _line([0.36, 0.26], [-0.36, 0.26], 20) + _line([-0.36, 0.26], [-0.36, 0.22], 4),
        # Grip
        _line([-0.03, 0.26], [-0.03, 0.52], 12) + _line([0.03, 0.26], [0.03, 0.52], 12),
        # Pommel gem
        _circle(0.0, 0.58, 0.07, 16)
    ]
    return strokes, "white"

def build_guitar():
    strokes = [
        # Upper bout
        _arc(-0.16, 0.08, 0.14, math.pi * 0.5, math.pi * 1.5, 16),
        _arc(0.16, 0.08, 0.14, -math.pi * 0.5, math.pi * 0.5, 16),
        # Lower bout
        _arc(-0.22, 0.38, 0.18, math.pi * 0.3, math.pi * 1.5, 20),
        _arc(0.22, 0.38, 0.18, -math.pi * 0.5, math.pi * 0.7, 20),
        # Bottom curve
        _arc(0.0, 0.46, 0.22, 0.0, math.pi, 20),
        # Sound hole
        _circle(0.0, 0.18, 0.08, 20),
        # Neck
        _line([-0.04, 0.0], [-0.04, -0.55], 24),
        _line([0.04, 0.0], [0.04, -0.55], 24),
        # Headstock
        _line([-0.06, -0.55], [0.06, -0.55], 6) + _line([0.06, -0.55], [0.05, -0.72], 10) + _line([0.05, -0.72], [-0.05, -0.72], 6) + _line([-0.05, -0.72], [-0.06, -0.55], 10),
        # Strings
        _line([0.0, -0.55], [0.0, 0.40], 24)
    ]
    return strokes, "gold"

def build_tree():
    strokes = [
        # Trunk & roots
        _line([-0.06, 0.05], [-0.08, 0.65], 18) + _line([-0.08, 0.65], [-0.22, 0.72], 8),
        _line([0.06, 0.05], [0.08, 0.65], 18) + _line([0.08, 0.65], [0.22, 0.72], 8),
        # Internal branches
        _line([-0.04, 0.20], [-0.18, -0.05], 12),
        _line([0.04, 0.20], [0.18, -0.05], 12),
        _line([0.0, 0.10], [0.0, -0.20], 12),
        # Foliage cloud layers
        _arc(-0.25, -0.15, 0.20, math.pi * 0.6, math.pi * 1.6, 16),
        _arc(0.25, -0.15, 0.20, -math.pi * 0.6, math.pi * 0.4, 16),
        _arc(-0.18, -0.38, 0.22, math.pi * 0.8, math.pi * 1.8, 16),
        _arc(0.18, -0.38, 0.22, -math.pi * 0.8, math.pi * 0.2, 16),
        _arc(0.0, -0.52, 0.24, -math.pi * 0.2, math.pi * 1.2, 20)
    ]
    return strokes, "green"

def build_butterfly():
    strokes = [
        # Body / thorax
        _ellipse(0.0, 0.05, 0.04, 0.28, 20),
        # Head & antennae
        _circle(0.0, -0.26, 0.05, 12),
        _arc(-0.06, -0.36, 0.12, math.pi * 0.2, math.pi * 0.9, 10),
        _arc(0.06, -0.36, 0.12, math.pi * 0.1, math.pi * 0.8, 10),
        # Top-left wing
        _arc(-0.35, -0.22, 0.32, -math.pi * 0.1, math.pi * 1.1, 28) + _line([-0.04, -0.15], [-0.35, -0.22], 12),
        # Bottom-left wing
        _arc(-0.28, 0.22, 0.22, math.pi * 0.2, math.pi * 1.3, 24) + _line([-0.04, 0.15], [-0.28, 0.22], 10),
        # Top-right wing
        _arc(0.35, -0.22, 0.32, -math.pi * 0.1, math.pi * 1.1, 28) + _line([0.04, -0.15], [0.35, -0.22], 12),
        # Bottom-right wing
        _arc(0.28, 0.22, 0.22, -math.pi * 0.3, math.pi * 0.8, 24) + _line([0.04, 0.15], [0.28, 0.22], 10)
    ]
    return strokes, "pink"

def build_dragon():
    strokes = [
        # Serpentine body
        _arc(-0.15, -0.20, 0.25, -math.pi * 0.3, math.pi * 0.8, 24),
        _arc(0.15, 0.10, 0.30, math.pi * 0.7, math.pi * 1.8, 24),
        _arc(-0.10, 0.40, 0.25, -math.pi * 0.2, math.pi * 0.9, 20),
        # Head with horn
        _line([-0.35, -0.45], [-0.55, -0.55], 10) + _line([-0.55, -0.55], [-0.30, -0.65], 10) + _line([-0.30, -0.65], [-0.20, -0.45], 8),
        # Fire breath
        _line([-0.55, -0.55], [-0.75, -0.60], 10) + _line([-0.55, -0.55], [-0.72, -0.50], 10) + _line([-0.55, -0.55], [-0.70, -0.68], 10),
        # Wings
        _line([0.0, -0.10], [0.45, -0.55], 16) + _line([0.45, -0.55], [0.35, -0.20], 10) + _line([0.45, -0.55], [0.60, -0.35], 12) + _line([0.60, -0.35], [0.20, -0.05], 14),
        # Tail barb
        _line([-0.10, 0.65], [-0.18, 0.72], 6) + _line([-0.18, 0.72], [-0.02, 0.72], 6) + _line([-0.02, 0.72], [-0.10, 0.65], 6)
    ]
    return strokes, "red"

def build_saturn():
    strokes = [
        # Central planet sphere
        _circle(0.0, 0.0, 0.35, 48),
        # Equator ring inner ellipse
        _ellipse(0.0, 0.0, 0.68, 0.18, 48, z=-0.05),
        # Equator ring outer ellipse
        _ellipse(0.0, 0.0, 0.85, 0.24, 56, z=0.05),
        # Ring edge connections
        _line([-0.85, 0.0], [-0.68, 0.0], 6),
        _line([0.68, 0.0], [0.85, 0.0], 6)
    ]
    return strokes, "gold"

def build_car():
    strokes = [
        # Body lower silhouette
        _line([-0.65, 0.18], [-0.65, 0.02], 6) + _line([-0.65, 0.02], [-0.35, 0.02], 12) + _line([-0.35, 0.02], [-0.18, -0.25], 14) + _line([-0.18, -0.25], [0.18, -0.25], 14) + _line([0.18, -0.25], [0.38, 0.02], 14) + _line([0.38, 0.02], [0.65, 0.02], 12) + _line([0.65, 0.02], [0.65, 0.18], 6),
        # Bottom chassis
        _line([-0.65, 0.18], [-0.48, 0.18], 6) + _line([-0.28, 0.18], [0.28, 0.18], 18) + _line([0.48, 0.18], [0.65, 0.18], 6),
        # Front wheel
        _circle(-0.38, 0.18, 0.12, 20),
        _circle(-0.38, 0.18, 0.05, 12),
        # Rear wheel
        _circle(0.38, 0.18, 0.12, 20),
        _circle(0.38, 0.18, 0.05, 12),
        # Windows & door line
        _line([-0.16, -0.22], [0.0, -0.22], 8) + _line([0.0, -0.22], [0.0, 0.02], 10) + _line([0.0, 0.02], [-0.30, 0.02], 12) + _line([-0.30, 0.02], [-0.16, -0.22], 10),
        _line([0.02, -0.22], [0.16, -0.22], 8) + _line([0.16, -0.22], [0.32, 0.02], 10) + _line([0.32, 0.02], [0.02, 0.02], 12) + _line([0.02, 0.02], [0.02, -0.22], 10)
    ]
    return strokes, "cyan"

def build_bicycle():
    strokes = [
        # Front wheel
        _circle(0.40, 0.20, 0.22, 28),
        _circle(0.40, 0.20, 0.04, 8),
        # Rear wheel
        _circle(-0.40, 0.20, 0.22, 28),
        _circle(-0.40, 0.20, 0.04, 8),
        # Frame triangle
        _line([-0.40, 0.20], [0.0, 0.20], 14), # Chainstay
        _line([0.0, 0.20], [-0.15, -0.15], 16), # Seat tube
        _line([-0.15, -0.15], [-0.40, 0.20], 16), # Seatstay
        _line([-0.15, -0.15], [0.25, -0.15], 16), # Top tube
        _line([0.0, 0.20], [0.25, -0.15], 18), # Down tube
        # Fork & handlebars
        _line([0.40, 0.20], [0.28, -0.28], 20),
        _line([0.22, -0.32], [0.34, -0.26], 10),
        # Seat
        _line([-0.22, -0.18], [-0.08, -0.18], 8)
    ]
    return strokes, "gold"

def build_house():
    strokes = [
        # Walls
        _line([-0.45, 0.55], [-0.45, -0.05], 18) + _line([-0.45, -0.05], [0.45, -0.05], 24) + _line([0.45, -0.05], [0.45, 0.55], 18) + _line([0.45, 0.55], [-0.45, 0.55], 24),
        # Roof triangle
        _line([-0.55, -0.05], [0.0, -0.58], 22) + _line([0.0, -0.58], [0.55, -0.05], 22) + _line([0.55, -0.05], [-0.55, -0.05], 26),
        # Chimney
        _line([0.22, -0.35], [0.22, -0.65], 10) + _line([0.22, -0.65], [0.32, -0.65], 6) + _line([0.32, -0.65], [0.32, -0.22], 8),
        # Door
        _line([-0.12, 0.55], [-0.12, 0.18], 12) + _line([-0.12, 0.18], [0.12, 0.18], 10) + _line([0.12, 0.18], [0.12, 0.55], 12),
        # Door knob
        _circle(0.06, 0.38, 0.02, 8),
        # Window
        _line([-0.38, 0.10], [-0.20, 0.10], 8) + _line([-0.20, 0.10], [-0.20, 0.28], 8) + _line([-0.20, 0.28], [-0.38, 0.28], 8) + _line([-0.38, 0.28], [-0.38, 0.10], 8),
        _line([-0.29, 0.10], [-0.29, 0.28], 6),
        _line([-0.38, 0.19], [-0.20, 0.19], 6)
    ]
    return strokes, "gold"

def build_umbrella():
    strokes = [
        # Canopy dome
        _arc(0.0, -0.15, 0.55, math.pi * 0.95, math.pi * 2.05, 36),
        # Scalloped bottom edge
        _arc(-0.38, -0.15, 0.18, 0.0, math.pi, 12),
        _arc(0.0, -0.15, 0.18, 0.0, math.pi, 12),
        _arc(0.38, -0.15, 0.18, 0.0, math.pi, 12),
        # Top ferrule tip
        _line([0.0, -0.70], [0.0, -0.76], 4),
        # Center shaft
        _line([0.0, -0.70], [0.0, 0.45], 36),
        # J-hook handle
        _arc(0.08, 0.45, 0.10, math.pi * 0.0, math.pi * 1.1, 14)
    ]
    return strokes, "cyan"

def build_lightning():
    strokes = [
        _line([0.25, -0.75], [-0.10, -0.10], 24),
        _line([-0.10, -0.10], [0.15, -0.10], 14),
        _line([0.15, -0.10], [-0.25, 0.75], 30),
        _line([-0.25, 0.75], [-0.02, 0.15], 22),
        _line([-0.02, 0.15], [-0.22, 0.15], 12),
        _line([-0.22, 0.15], [0.25, -0.75], 26)
    ]
    return strokes, "yellow"

def build_ghost():
    strokes = [
        # Head dome
        _arc(0.0, -0.25, 0.38, math.pi * 0.95, math.pi * 2.05, 32),
        # Left & right sides
        _line([-0.38, -0.25], [-0.42, 0.45], 20),
        _line([0.38, -0.25], [0.42, 0.45], 20),
        # Wavy bottom hem
        _arc(-0.28, 0.45, 0.14, 0.0, math.pi, 12),
        _arc(0.0, 0.45, 0.14, math.pi, 2.0 * math.pi, 12),
        _arc(0.28, 0.45, 0.14, 0.0, math.pi, 12),
        # Big hollow eyes
        _ellipse(-0.16, -0.20, 0.06, 0.10, 16),
        _ellipse(0.16, -0.20, 0.06, 0.10, 16),
        # O-mouth
        _ellipse(0.0, 0.02, 0.05, 0.08, 14)
    ]
    return strokes, "white"

def build_robot():
    strokes = [
        # Head box
        _line([-0.25, -0.40], [0.25, -0.40], 14) + _line([0.25, -0.40], [0.25, -0.70], 12) + _line([0.25, -0.70], [-0.25, -0.70], 14) + _line([-0.25, -0.70], [-0.25, -0.40], 12),
        # Antenna
        _line([0.0, -0.70], [0.0, -0.82], 6),
        _circle(0.0, -0.84, 0.03, 8),
        # Eyes
        _circle(-0.12, -0.55, 0.05, 12),
        _circle(0.12, -0.55, 0.05, 12),
        # Mouth grid
        _line([-0.14, -0.46], [0.14, -0.46], 8),
        # Neck
        _line([-0.08, -0.40], [-0.08, -0.32], 4),
        _line([0.08, -0.40], [0.08, -0.32], 4),
        # Body torso
        _line([-0.35, -0.32], [0.35, -0.32], 18) + _line([0.35, -0.32], [0.35, 0.45], 20) + _line([0.35, 0.45], [-0.35, 0.45], 18) + _line([-0.35, 0.45], [-0.35, -0.32], 20),
        # Chest dials & meters
        _circle(0.0, 0.0, 0.14, 18),
        _line([0.0, 0.0], [0.08, -0.06], 6),
        # Jointed arms
        _line([-0.35, -0.22], [-0.55, 0.0], 12) + _line([-0.55, 0.0], [-0.48, 0.25], 10),
        _line([0.35, -0.22], [0.55, 0.0], 12) + _line([0.55, 0.0], [0.48, 0.25], 10),
        # Claws
        _arc(-0.48, 0.28, 0.06, 0.0, math.pi, 8),
        _arc(0.48, 0.28, 0.06, 0.0, math.pi, 8)
    ]
    return strokes, "cyan"

def build_alien():
    strokes = [
        # Bulbous alien head contour
        _arc(0.0, -0.25, 0.46, math.pi * 0.85, math.pi * 2.15, 36) + _line([0.42, -0.10], [0.08, 0.40], 16) + _line([0.08, 0.40], [-0.08, 0.40], 6) + _line([-0.08, 0.40], [-0.42, -0.10], 16),
        # Giant tilted almond eyes
        _ellipse(-0.18, -0.12, 0.14, 0.22, 24),
        _ellipse(0.18, -0.12, 0.14, 0.22, 24),
        # Tiny slit mouth
        _line([-0.06, 0.28], [0.06, 0.28], 6),
        # Slender neck
        _line([-0.06, 0.40], [-0.06, 0.62], 8),
        _line([0.06, 0.40], [0.06, 0.62], 8)
    ]
    return strokes, "lime"

def build_coffee_cup():
    strokes = [
        # Mug rim ellipse
        _ellipse(0.0, -0.10, 0.32, 0.08, 24),
        # Mug body & base
        _line([-0.32, -0.10], [-0.28, 0.45], 18) + _arc(0.0, 0.45, 0.28, 0.0, math.pi, 18) + _line([0.28, 0.45], [0.32, -0.10], 18),
        # Handle
        _arc(0.32, 0.15, 0.18, -math.pi * 0.4, math.pi * 0.5, 18),
        _arc(0.32, 0.15, 0.24, -math.pi * 0.4, math.pi * 0.5, 18),
        # Rising steam swirls
        _arc(-0.12, -0.35, 0.10, math.pi * 0.2, math.pi * 1.5, 12),
        _arc(0.08, -0.42, 0.12, -math.pi * 0.3, math.pi * 1.2, 14)
    ]
    return strokes, "gold"

def build_crown():
    strokes = [
        # Circlet base
        _line([-0.45, 0.35], [0.45, 0.35], 24),
        _line([-0.45, 0.42], [0.45, 0.42], 24),
        # Crown peaks
        _line([-0.45, 0.35], [-0.42, -0.15], 16) + _line([-0.42, -0.15], [-0.22, 0.12], 12) + _line([-0.22, 0.12], [0.0, -0.32], 16) + _line([0.0, -0.32], [0.22, 0.12], 16) + _line([0.22, 0.12], [0.42, -0.15], 12) + _line([0.42, -0.15], [0.45, 0.35], 16),
        # Jewel circles on peaks
        _circle(-0.42, -0.18, 0.04, 10),
        _circle(0.0, -0.36, 0.05, 12),
        _circle(0.42, -0.18, 0.04, 10),
        # Base gems
        _circle(-0.20, 0.38, 0.02, 8),
        _circle(0.0, 0.38, 0.02, 8),
        _circle(0.20, 0.38, 0.02, 8)
    ]
    return strokes, "gold"

def build_diamond():
    strokes = [
        # Table top
        _line([-0.30, -0.45], [0.30, -0.45], 16),
        # Crown facets
        _line([-0.30, -0.45], [-0.55, -0.15], 14),
        _line([0.30, -0.45], [0.55, -0.15], 14),
        _line([-0.55, -0.15], [0.55, -0.15], 26),
        # Star facet diagonals
        _line([-0.10, -0.45], [-0.20, -0.15], 10),
        _line([0.10, -0.45], [0.20, -0.15], 10),
        # Pavilion to bottom culet
        _line([-0.55, -0.15], [0.0, 0.65], 28),
        _line([0.55, -0.15], [0.0, 0.65], 28),
        _line([-0.20, -0.15], [0.0, 0.65], 24),
        _line([0.20, -0.15], [0.0, 0.65], 24)
    ]
    return strokes, "cyan"

def build_clock():
    strokes = [
        # Outer bezel
        _circle(0.0, 0.0, 0.52, 48),
        _circle(0.0, 0.0, 0.48, 48),
        # Center pivot
        _circle(0.0, 0.0, 0.04, 12),
        # Hour hand (pointing to 10)
        _line([0.0, 0.0], [-0.18, -0.18], 14),
        # Minute hand (pointing to 2)
        _line([0.0, 0.0], [0.22, -0.28], 18),
        # 12, 3, 6, 9 tick marks
        _line([0.0, -0.48], [0.0, -0.40], 6),
        _line([0.48, 0.0], [0.40, 0.0], 6),
        _line([0.0, 0.48], [0.0, 0.40], 6),
        _line([-0.48, 0.0], [-0.40, 0.0], 6)
    ]
    return strokes, "gold"

def build_glasses():
    strokes = [
        # Left rim
        _ellipse(-0.28, 0.0, 0.22, 0.16, 28),
        # Right rim
        _ellipse(0.28, 0.0, 0.22, 0.16, 28),
        # Nose bridge arch
        _arc(0.0, -0.02, 0.08, math.pi * 0.2, math.pi * 0.8, 10),
        # Left temple arm
        _line([-0.50, 0.0], [-0.72, -0.15], 12),
        # Right temple arm
        _line([0.50, 0.0], [0.72, -0.15], 12)
    ]
    return strokes, "purple"

def build_dna():
    strokes = []
    # Two intertwining sinusoidal strands in 3D
    strand1, strand2 = [], []
    for i in range(50):
        t = (i / 49.0) * 4.0 * math.pi
        y = -0.70 + (i / 49.0) * 1.40
        x1 = math.cos(t) * 0.32
        z1 = math.sin(t) * 0.32
        strand1.append([x1, y, z1])
        strand2.append([-x1, y, -z1])
        # Base pair rungs every 5 steps
        if i % 4 == 0:
            strokes.append([[x1, y, z1], [-x1, y, -z1]])
    strokes.append(strand1)
    strokes.append(strand2)
    return strokes, "cyan"

def build_snowflake():
    strokes = []
    # 6 radial arms with branches
    for a_idx in range(6):
        a = (a_idx / 6.0) * 2.0 * math.pi
        cos_a = math.cos(a)
        sin_a = math.sin(a)
        # Main arm
        strokes.append([[0.0, 0.0, 0.0], [cos_a * 0.65, sin_a * 0.65, 0.0]])
        # Side needles
        for r_frac in [0.35, 0.52]:
            px = cos_a * (r_frac * 0.65)
            py = sin_a * (r_frac * 0.65)
            branch_a1 = a + math.pi * 0.25
            branch_a2 = a - math.pi * 0.25
            strokes.append([[px, py, 0.0], [px + math.cos(branch_a1) * 0.12, py + math.sin(branch_a1) * 0.12, 0.0]])
            strokes.append([[px, py, 0.0], [px + math.cos(branch_a2) * 0.12, py + math.sin(branch_a2) * 0.12, 0.0]])
    return strokes, "white"

def build_campfire():
    strokes = [
        # Crossed logs at base
        _line([-0.45, 0.45], [0.45, 0.25], 16),
        _line([-0.45, 0.25], [0.45, 0.45], 16),
        _line([-0.35, 0.35], [0.35, 0.35], 16),
        # Center tall flame
        _arc(-0.10, 0.10, 0.25, -math.pi * 0.3, math.pi * 0.5, 14) + _line([-0.10, -0.15], [0.0, -0.65], 16) + _line([0.0, -0.65], [0.10, -0.15], 16) + _arc(0.10, 0.10, 0.25, math.pi * 0.5, math.pi * 1.3, 14),
        # Left flickering flame
        _arc(-0.18, 0.15, 0.18, 0.0, math.pi * 0.8, 12) + _line([-0.22, -0.02], [-0.15, -0.42], 12) + _line([-0.15, -0.42], [-0.08, 0.02], 10),
        # Right flickering flame
        _arc(0.18, 0.15, 0.18, math.pi * 0.2, math.pi, 12) + _line([0.22, -0.02], [0.15, -0.42], 12) + _line([0.15, -0.42], [0.08, 0.02], 10)
    ]
    return strokes, "red"

def build_sun():
    strokes = [
        # Central sun core
        _circle(0.0, 0.0, 0.30, 36)
    ]
    # 8 radiating rays
    for i in range(8):
        a = (i / 8.0) * 2.0 * math.pi
        strokes.append([
            [math.cos(a) * 0.36, math.sin(a) * 0.36, 0.0],
            [math.cos(a) * 0.65, math.sin(a) * 0.65, 0.0]
        ])
    return strokes, "gold"

def build_moon():
    strokes = [
        # Outer crescent arc
        _arc(0.0, 0.0, 0.55, -math.pi * 0.45, math.pi * 0.45, 36),
        # Inner scooped arc
        _arc(0.20, 0.0, 0.46, -math.pi * 0.35, math.pi * 0.35, 32),
        # Small twinkle stars
        _line([-0.35, -0.35], [-0.35, -0.25], 4),
        _line([-0.40, -0.30], [-0.30, -0.30], 4),
        _line([-0.25, 0.25], [-0.25, 0.35], 4),
        _line([-0.30, 0.30], [-0.20, 0.30], 4)
    ]
    return strokes, "gold"

# -------------------------------------------------------------
# Dynamic LLM Vector Stroke Parser (Draw ANYTHING!)
# -------------------------------------------------------------

def parse_llm_vector_strokes(text: str):
    """Parses vector instructions from LLM output like:
       [strokes: line(-0.5,-0.5, 0.5,0.5); circle(0,0,0.3); arc(0,0,0.4, 0, 3.14); ...]
    """
    m = re.search(r"\[strokes\s*:\s*(.*?)\]", text, re.IGNORECASE | re.DOTALL)
    if not m:
        return None

    cmd_block = m.group(1)
    strokes = []

    # line(x1, y1, x2, y2) or line(x1, y1, z1, x2, y2, z2)
    for match in re.finditer(r"line\s*\(\s*([-0-9.]+)\s*,\s*([-0-9.]+)(?:\s*,\s*([-0-9.]+))?\s*,\s*([-0-9.]+)\s*,\s*([-0-9.]+)(?:\s*,\s*([-0-9.]+))?\s*\)", cmd_block):
        g = match.groups()
        x1, y1 = float(g[0]), float(g[1])
        z1 = float(g[2]) if g[2] is not None else 0.0
        x2, y2 = float(g[3]), float(g[4])
        z2 = float(g[5]) if g[5] is not None else 0.0
        strokes.append(_line([x1, y1, z1], [x2, y2, z2], 12))

    # circle(cx, cy, r)
    for match in re.finditer(r"circle\s*\(\s*([-0-9.]+)\s*,\s*([-0-9.]+)\s*,\s*([-0-9.]+)\s*\)", cmd_block):
        cx, cy, r = float(match.group(1)), float(match.group(2)), float(match.group(3))
        strokes.append(_circle(cx, cy, r, 24))

    # arc(cx, cy, r, a1, a2)
    for match in re.finditer(r"arc\s*\(\s*([-0-9.]+)\s*,\s*([-0-9.]+)\s*,\s*([-0-9.]+)\s*,\s*([-0-9.]+)\s*,\s*([-0-9.]+)\s*\)", cmd_block):
        cx, cy, r = float(match.group(1)), float(match.group(2)), float(match.group(3))
        a1, a2 = float(match.group(4)), float(match.group(5))
        strokes.append(_arc(cx, cy, r, a1, a2, 16))

    return strokes if strokes else None

# -------------------------------------------------------------
# Subject Matching & Universal Fallback Synthesizer
# -------------------------------------------------------------

SEMANTIC_BUILDERS = {
    "rocket": build_rocket,
    "spaceship": build_rocket,
    "shuttle": build_rocket,
    "airplane": build_airplane,
    "aeroplane": build_airplane,
    "plane": build_airplane,
    "jet": build_airplane,
    "sword": build_sword,
    "blade": build_sword,
    "dagger": build_sword,
    "excalibur": build_sword,
    "guitar": build_guitar,
    "instrument": build_guitar,
    "violin": build_guitar,
    "tree": build_tree,
    "bonsai": build_tree,
    "forest": build_tree,
    "butterfly": build_butterfly,
    "dragon": build_dragon,
    "saturn": build_saturn,
    "planet": build_saturn,
    "cosmos": build_saturn,
    "car": build_car,
    "automobile": build_car,
    "vehicle": build_car,
    "bicycle": build_bicycle,
    "bike": build_bicycle,
    "house": build_house,
    "home": build_house,
    "cabin": build_house,
    "umbrella": build_umbrella,
    "parasol": build_umbrella,
    "lightning": build_lightning,
    "thunder": build_lightning,
    "bolt": build_lightning,
    "ghost": build_ghost,
    "spooky": build_ghost,
    "robot": build_robot,
    "android": build_robot,
    "cyborg": build_robot,
    "alien": build_alien,
    "ufo": build_alien,
    "martian": build_alien,
    "coffee": build_coffee_cup,
    "cup": build_coffee_cup,
    "tea": build_coffee_cup,
    "mug": build_coffee_cup,
    "crown": build_crown,
    "tiara": build_crown,
    "king": build_crown,
    "diamond": build_diamond,
    "gem": build_diamond,
    "crystal": build_diamond,
    "clock": build_clock,
    "watch": build_clock,
    "time": build_clock,
    "glasses": build_glasses,
    "spectacles": build_glasses,
    "sunglasses": build_glasses,
    "dna": build_dna,
    "helix": build_dna,
    "gene": build_dna,
    "snowflake": build_snowflake,
    "snow": build_snowflake,
    "campfire": build_campfire,
    "fire": build_campfire,
    "flame": build_campfire,
    "sun": build_sun,
    "sol": build_sun,
    "moon": build_moon,
    "crescent": build_moon
}

def generate_universal_silhouette(query: str):
    """Constructs a custom geometric silhouette for arbitrary subjects outside the library."""
    # Deterministic hash of the prompt for reproducible creativity
    h = abs(hash(query.strip().lower()))
    color_choices = ["cyan", "gold", "pink", "purple", "emerald", "white", "yellow"]
    color = color_choices[h % len(color_choices)]

    strokes = []
    # Primary outer contour based on geometric harmony
    num_lobes = 3 + (h % 5)
    r_base = 0.35 + ((h >> 2) % 3) * 0.08
    r_amp = 0.15 + ((h >> 4) % 3) * 0.06

    pts = []
    steps = 60
    for i in range(steps):
        a = (i / float(steps)) * 2.0 * math.pi
        r = r_base + math.sin(a * num_lobes) * r_amp
        pts.append([math.cos(a) * r, math.sin(a) * r, 0.0])
    pts.append(pts[0])
    strokes.append(pts)

    # Internal core structure
    core_type = (h >> 6) % 3
    if core_type == 0:
        strokes.append(_circle(0.0, 0.0, 0.16, 24))
    elif core_type == 1:
        strokes.append(_line([-0.25, 0.0], [0.25, 0.0], 12))
        strokes.append(_line([0.0, -0.25], [0.0, 0.25], 12))
    else:
        strokes.append(_ellipse(0.0, 0.0, 0.22, 0.10, 20))

    return strokes, color

def synthesize_custom_shape(query: str, ai_response: str = ""):
    """Takes user prompt and AI response, detects intent, and generates 600 particle coordinates."""
    # 1. Check if LLM explicitly emitted vector stroke commands
    llm_strokes = parse_llm_vector_strokes(ai_response)
    if llm_strokes:
        pts = sample_strokes_to_particles(llm_strokes)
        return pts, "CUSTOM", "cyan"

    # 2. Check semantic dictionary
    q_clean = re.sub(r"^(hey\s+sphere|sphere|can\s+you\s+draw|can\s+you\s+make|draw\s+me|make\s+me|draw\s+a|draw\s+an|draw|make\s+a|make\s+an|make)\s+", "", query, flags=re.IGNORECASE).strip()
    words = re.findall(r"[a-z]+", q_clean.lower())

    for w in words:
        if w in SEMANTIC_BUILDERS:
            strokes, color = SEMANTIC_BUILDERS[w]()
            pts = sample_strokes_to_particles(strokes)
            return pts, w.upper(), color

    # Also check full query words in reverse
    for word, builder in SEMANTIC_BUILDERS.items():
        if word in query.lower():
            strokes, color = builder()
            pts = sample_strokes_to_particles(strokes)
            return pts, word.upper(), color

    # 3. Universal Fallback: Generates a unique customized geometric drawing
    subject_label = q_clean.split()[0].upper() if q_clean else "FIGURE"
    if len(subject_label) > 12:
        subject_label = subject_label[:12]
    strokes, color = generate_universal_silhouette(query)
    pts = sample_strokes_to_particles(strokes)
    return pts, subject_label, color
