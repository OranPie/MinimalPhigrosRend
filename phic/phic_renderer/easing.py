from __future__ import annotations

import math

def ease_01(t):  return t
def ease_02(t):  return math.sin(math.pi*t/2)
def ease_03(t):  return 1 - math.cos(math.pi*t/2)
def ease_04(t):  return 1 - (1-t)*(1-t)
def ease_05(t):  return t*t
def ease_06(t):  return -(math.cos(math.pi*t) - 1)/2

def ease_07(t):  return 2*t*t if t < 0.5 else 1 - ((-2*t + 2)**2)/2
def ease_08(t):  return 1 - (1-t)**3
def ease_09(t):  return t**3
def ease_10(t):  return 1 - (1-t)**4
def ease_11(t):  return t**4
def ease_12(t):  return 4*(t**3) if t < 0.5 else 1 - ((-2*t + 2)**3)/2
def ease_13(t):  return 8*(t**4) if t < 0.5 else 1 - ((-2*t + 2)**4)/2
def ease_14(t):  return 1 - (1-t)**5
def ease_15(t):  return t**5

def ease_16(t):  return 1 if t == 1 else 1 - 2**(-10*t)
def ease_17(t):  return 0 if t == 0 else 2**(10*t - 10)
def ease_18(t):  return (1 - (t-1)*(t-1))**0.5
def ease_19(t):  return 1 - (1 - t*t)**0.5

def ease_20(t):  x=t-1; return 1 + 2.70158*(x**3) + 1.70158*(x**2)
def ease_21(t):  return 2.70158*(t**3) - 1.70158*(t**2)

def ease_22(t):
    if t < 0.5: return (1 - (1 - (2*t)**2)**0.5)/2
    return ((1 - (-2*t + 2)**2)**0.5 + 1)/2

def ease_23(t):
    s=2.5949095
    if t < 0.5:
        x=2*t; return (x*x*((s+1)*x - s))/2
    x=2*t-2; return (x*x*((s+1)*x + s) + 2)/2

def ease_24(t):
    if t==0: return 0
    if t==1: return 1
    return 2**(-10*t)*math.sin((t*10 - 0.75)*(2*math.pi/3)) + 1

def ease_25(t):
    if t==0: return 0
    if t==1: return 1
    return -2**(10*t - 10)*math.sin((t*10 - 10.75)*(2*math.pi/3))

def ease_26(t):
    if t < 1/2.75: return 7.5625*t*t
    if t < 2/2.75: x=t-1.5/2.75;  return 7.5625*x*x + 0.75
    if t < 2.5/2.75: x=t-2.25/2.75; return 7.5625*x*x + 0.9375
    x=t-2.625/2.75; return 7.5625*x*x + 0.984375

def ease_27(t):  return 1 - ease_26(1 - t)

def ease_28(t):
    return (1 - ease_26(1 - 2*t))/2 if t < 0.5 else (1 + ease_26(2*t - 1))/2

def ease_29(t):
    if t==0: return 0
    if t==1: return 1
    k=(2*math.pi)/4.5
    if t < 0.5: return -(2**(20*t - 10)*math.sin((20*t - 11.125)*k))/2
    return (2**(-20*t + 10)*math.sin((20*t - 11.125)*k))/2 + 1

def easing_from_type(tp: int) -> Callable[[float], float]:
    # Minimal mapping. Extend later for full RPE coverage.
    m = {
        0: ease_01,
        1: ease_01,
        2: ease_02,
        3: ease_03,
        4: ease_04,
        5: ease_05,
        6: ease_06,
        7: ease_07,
        8: ease_08,
        9: ease_09,
        10: ease_10,
        11: ease_11,
        12: ease_12,
        13: ease_13,
        14: ease_14,
        15: ease_15,
        16: ease_16,
        17: ease_17,
        18: ease_18,
        19: ease_19,
        20: ease_20,
        21: ease_21,
        22: ease_22,
        23: ease_23,
        24: ease_24,
        25: ease_25,
        26: ease_26,
        27: ease_27,
        28: ease_28,
        29: ease_29,
    }
    return m.get(tp, ease_01)


# Global easing shift for RPE easingType (some exporters are 1-based)
rpe_easing_shift: int = 0

def cubic_bezier_y_for_x(x1, y1, x2, y2, x, iters=18):
    # Solve u s.t. Bx(u)=x by binary search, then return By(u).
    # Control points: (0,0), (x1,y1), (x2,y2), (1,1)
    def bx(u):
        a = 1-u
        return 3*a*a*u*x1 + 3*a*u*u*x2 + u*u*u
    def by(u):
        a = 1-u
        return 3*a*a*u*y1 + 3*a*u*u*y2 + u*u*u

    lo, hi = 0.0, 1.0
    for _ in range(iters):
        mid = (lo + hi) * 0.5
        if bx(mid) < x:
            lo = mid
        else:
            hi = mid
    return by((lo + hi) * 0.5)




def set_rpe_easing_shift(shift: int) -> None:
    global rpe_easing_shift
    rpe_easing_shift = int(shift)
