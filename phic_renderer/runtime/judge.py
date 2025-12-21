from __future__ import annotations

JUDGE_WEIGHT = {
    "PERFECT": 1.0,
    "GOOD": 0.6,
    "BAD": 0.0,
    "MISS": 0.0,
}

class Judge:
    PERFECT = 0.045
    GOOD    = 0.090
    BAD     = 0.150

    def __init__(self):
        self.combo = 0
        self.max_combo = 0
        self.acc_sum = 0.0     # cumulative weight
        self.judged_cnt = 0    # judged count
        self.hit_total = 0

    def bump(self):
        self.hit_total += 1
        self.combo += 1
        self.max_combo = max(self.max_combo, self.combo)

    def break_combo(self):
        self.combo = 0

    def try_hit(self, ns: NoteState, t: float) -> Optional[str]:
        dt = abs(t - ns.note.t_hit)
        if dt <= self.PERFECT:
            self.bump()
            ns.judged = True
            ns.hit = True
            self.acc_sum += JUDGE_WEIGHT["PERFECT"]
            self.judged_cnt += 1
            return "PERFECT"
        if dt <= self.GOOD:
            self.bump()
            ns.judged = True
            ns.hit = True
            self.acc_sum += JUDGE_WEIGHT["GOOD"]
            self.judged_cnt += 1
            return "GOOD"
        if dt <= self.BAD:
            self.break_combo()
            ns.judged = True
            ns.hit = True
            self.acc_sum += JUDGE_WEIGHT["BAD"]
            self.judged_cnt += 1
            return "BAD"
        return None

    def grade_window(self, t_note: float, t: float) -> Optional[str]:
        dt = abs(t - t_note)
        if dt <= self.PERFECT:
            return "PERFECT"
        if dt <= self.GOOD:
            return "GOOD"
        if dt <= self.BAD:
            return "BAD"
        return None

    def mark_miss(self, ns: NoteState):
        ns.judged = True
        ns.miss = True
        self.break_combo()
        self.acc_sum += JUDGE_WEIGHT["MISS"]
        self.judged_cnt += 1


