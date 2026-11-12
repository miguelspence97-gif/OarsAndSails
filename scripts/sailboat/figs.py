# seat marker figures, game units, root at the origin, facing +y (z up)
import numpy as np
from scipy.spatial import ConvexHull
def hullpart(pts):
    P = np.asarray(pts, float); h = ConvexHull(P); return P, h.simplices.copy()
def limb(p0, p1, r0, r1=None, seg=8):
    r1 = r0 if r1 is None else r1; p0 = np.asarray(p0, float); p1 = np.asarray(p1, float)
    ax = p1 - p0; ax /= np.linalg.norm(ax); e1 = np.cross(ax, [1, 0, 0] if abs(ax[0]) < 0.9 else [0, 1, 0]); e1 /= np.linalg.norm(e1); e2 = np.cross(ax, e1)
    pts = []
    for c, r in ((p0, r0), (p1, r1)):
        for k in range(seg):
            a = 2 * np.pi * k / seg; pts.append(c + r * (np.cos(a) * e1 + np.sin(a) * e2))
    pts += [p0 - ax * r0 * 0.6, p1 + ax * r1 * 0.6]  # rounded ends
    return hullpart(pts)
def ball(c, r, seg=10):
    pts = []
    for i in range(1, seg):
        th = np.pi * i / seg
        for k in range(seg):
            ph = 2 * np.pi * k / seg; pts.append(np.asarray(c) + r * np.array([np.sin(th) * np.cos(ph), np.sin(th) * np.sin(ph), np.cos(th)]))
    pts += [np.asarray(c) + [0, 0, r], np.asarray(c) - [0, 0, r]]
    return hullpart(pts)
def box(x0, x1, y0, y1, z0, z1): return hullpart([(x, y, z) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)])
def arrow(z=0.3, s=1.0):  # the seat's root: a flat arrow on the floor pointing the way they face
    return hullpart([(-6 * s, -6 * s, z - 0.3), (6 * s, -6 * s, z - 0.3), (0, 10 * s, z - 0.3), (-6 * s, -6 * s, z + 0.5), (6 * s, -6 * s, z + 0.5), (0, 10 * s, z + 0.5)])
def chair():
    P = [arrow()]
    P.append(limb((0, -4, 27), (0, -6, 60), 10.0, 11.0))          # pelvis to chest
    P.append(ball((0, -4, 71), 7.5))                               # head
    for s in (-1, 1):
        P.append(limb((s * 6, -2, 28), (s * 7, 21, 29), 5.0, 4.2))  # thigh
        P.append(limb((s * 7, 21, 27), (s * 7, 23, 3), 3.8, 3.0))   # shin
        P.append(box(s * 7 - 3, s * 7 + 3, 19, 33, 0.3, 4.0))      # foot
        P.append(limb((s * 13, -5, 57), (s * 14, 3, 40), 3.2, 2.8)) # upper arm
        P.append(limb((s * 14, 3, 40), (s * 9, 17, 33), 2.8, 2.4))  # forearm, hand on the thigh
    return P
def ledge():
    # v1.8.52: the game's ledge sit puts the body about 15 ahead of its root and 12 below it (Miguel's tuning in game):
    # the figure drawn there, the arrow at the root
    P = [arrow()]
    def at(part): Q, F = part; return Q + np.array([0.0, 15.0, -12.0]), F
    P.append(at(limb((0, -6, 2), (0, -8, 34), 10.0, 11.0)))
    P.append(at(ball((0, -6, 45), 7.5)))
    for s in (-1, 1):
        P.append(at(limb((s * 6, -4, 4), (s * 7, 18, 5), 5.0, 4.2)))  # thigh along the edge's top
        P.append(at(limb((s * 7, 19, 3), (s * 7, 23, -38), 3.8, 3.0)))  # shin hanging over
        P.append(at(box(s * 7 - 3, s * 7 + 3, 21, 33, -42, -38.5)))  # foot
        P.append(at(limb((s * 13, -7, 31), (s * 15, -6, 14), 3.2, 2.8)))
        P.append(at(limb((s * 15, -6, 14), (s * 15, -2, 1), 2.8, 2.4)))  # hands on the edge beside him
    return P
def dog():
    P = [arrow(s=0.7)]
    P.append(limb((0, -8, 9), (0, 7, 25), 7.5, 6.0))               # haunch to chest, sitting up
    P.append(ball((0, 10, 33), 5.0))                               # head
    P.append(limb((0, 12, 32), (0, 19, 30), 2.6, 2.0))             # snout
    for s in (-1, 1):
        P.append(limb((s * 4, 8, 20), (s * 4, 10, 1), 2.0, 1.8))   # front legs
        P.append(limb((s * 6, -6, 7), (s * 5, 4, 1), 3.5, 2.0))    # hind legs folded
        P.append(hullpart([(s * 2, 9, 37), (s * 6, 7, 37), (s * 4, 8, 43), (s * 4, 10, 37)]))  # ears
    P.append(limb((0, -13, 5), (0, -24, 2), 1.6, 1.0))             # tail
    return P
