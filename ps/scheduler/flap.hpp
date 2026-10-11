// flap.hpp — positions of the two belt-end flaps (总体设计 §6: 两级翻板串联, stage 1 recheck vs NG, stage 2 rework vs
// scrap) from the picker's flap events. Pure logic (the app writes the pulse widths to des_pwm).
//
// Outlets (des_app.json "outlets", index): 0 recheck, 1 scrap, 2 rework. A part's flap settings are applied when the
// belt is prepare_mm before its event position and held until it is clear_mm past it; then the next event is served.
// Between parts the flaps rest in the recheck position (anything unplanned falls into recheck).
#pragma once

#include <vector>

#include "picker.hpp"

namespace des {

struct FlapConfig {
    bool enabled = false;          // ⚠ on once the flaps are mounted and their pulse widths measured
    float prepare_mm = 40;         // set the flaps this far before the part reaches the flap point
    float clear_mm = 60;           // keep them until the part is this far past it
    int f1_pass_us = 1000;         // stage 1: part goes on to the recheck chute
    int f1_ng_us = 1900;           //          part diverted to stage 2
    int f2_rework_us = 1000;       // stage 2: rework box
    int f2_scrap_us = 1900;        //          scrap box
};

class FlapSequencer {
public:
    explicit FlapSequencer(FlapConfig cfg) : cfg_(cfg) {}
    struct State {
        int us[2] = {0, 0};        // pulse widths for flap 1 / flap 2
        int track_id = -1;         // part the flaps are set for (-1 = resting)
        int outlet = 0;
    };
    // events: the picker's pending flap events (ordered by belt_mm); belt_mm: current belt travel
    const State &update(const std::vector<FlapEvent> &events, double belt_mm);
    const State &state() const { return st_; }
    uint64_t served = 0;           // parts the flaps were set for

private:
    void set_for(int outlet);
    FlapConfig cfg_;
    State st_;
    double hold_until_ = -1e18;
    std::vector<int> done_;        // track ids already served (recent)
};

}  // namespace des
