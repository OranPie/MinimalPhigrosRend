#pragma once
#include "phigros/core/types.hpp"
#include <vector>
#include <algorithm>

namespace phigros::engine {

class NoteManager {
public:
    NoteManager() = default;
    NoteManager(const std::vector<Note>* notes, std::vector<NoteState>* states)
        : notes_(notes), states_(states) {}

    void update_visibility(double t, double approach_time,
                           bool cull_enter_time = true) {
        visible_.clear();
        if (!notes_) return;

        // Binary search bounds — notes are sorted by t_hit (6B2)
        auto lo_it = std::lower_bound(notes_->begin(), notes_->end(),
            t - approach_time - 1.0,
            [](const Note& n, double v) { return n.t_hit < v; });
        auto hi_it = std::upper_bound(notes_->begin(), notes_->end(),
            t + approach_time,
            [](double v, const Note& n) { return v < n.t_hit; });

        for (auto it = lo_it; it != hi_it; ++it) {
            const auto& note = *it;
            const size_t i = static_cast<size_t>(it - notes_->begin());

            if (cull_enter_time) {
                if (t < note.t_enter) continue;
                if (t > note.t_end + 0.5) continue;
            }

            if (t < note.t_hit - approach_time) continue;

            visible_.push_back(static_cast<int>(i));
        }
    }

    const std::vector<int>& get_visible_indices() const { return visible_; }
    int get_visible_count() const { return static_cast<int>(visible_.size()); }
    int get_note_count() const { return notes_ ? static_cast<int>(notes_->size()) : 0; }

    // Binary search for first note with t_hit > t (notes sorted by t_hit)
    int find_next_note_index(double t) const {
        if (!notes_) return 0;
        auto it = std::upper_bound(notes_->begin(), notes_->end(), t,
            [](double t, const Note& n) { return t < n.t_hit; });
        return static_cast<int>(it - notes_->begin());
    }

    // Get notes in time range [t_start, t_end]
    std::vector<int> get_notes_in_range(double t_start, double t_end) const {
        std::vector<int> result;
        if (!notes_) return result;
        for (size_t i = 0; i < notes_->size(); ++i) {
            double th = (*notes_)[i].t_hit;
            if (th >= t_start && th <= t_end)
                result.push_back(static_cast<int>(i));
        }
        return result;
    }

private:
    const std::vector<Note>* notes_ = nullptr;
    std::vector<NoteState>* states_ = nullptr;
    std::vector<int> visible_;
};

} // namespace phigros::engine
