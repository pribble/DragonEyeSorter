// flap.hpp — side diverter gates (docs/运动控制与分拣.md「NG 分流」): up to two servo paddles on the belt edge,
// downstream of the pick line. A closed paddle lies across the part lane at 45 deg and the belt pushes whatever reaches
// it along the paddle into the bin beside the belt; open, it is turned away from the belt. Parts no gate takes ride on
// to the belt end = recheck box (fail-safe: an unplanned part, a lost track or a dead gate never ends up in scrap).
// Pure logic: the picker emits one event per NG / recheck part (with its gate, or -1 = belt end) and this class turns
// them into pulse widths for des_pwm channel = gate index.
#pragma once

#include <string>
#include <vector>

#include "picker.hpp"

namespace des {

struct FlapConfig {
    bool enabled = false;          // ⚠ on once the gates are mounted and their pulse widths measured
    struct Gate {
        float after_pick_mm = 70;  // contact point (closed paddle crosses the lane centre), downstream of the pick line
        std::string outlet_name = "scrap";
        int outlet = 1;            // index into AppConfig::outlets (resolved from outlet_name in load_config)
        int open_us = 1000;        // paddle turned away from the belt
        int closed_us = 1900;      // paddle across the lane
    };
    std::vector<Gate> gates;       // des_pwm channel = index; at most 2
    float prepare_mm = 110;        // close this far before the part reaches the contact point (paddle sweep + turn)
    float clear_mm = 190;          // reopen this far after it (the part has slid along the paddle off the belt)
    float gap_mm = 40;             // conflict margin upstream of the closing window (see Picker)
};

class FlapSequencer {
public:
    explicit FlapSequencer(FlapConfig cfg) : cfg_(std::move(cfg)) {}
    struct State {
        int us[2] = {0, 0};        // pulse width per gate (0 = no such gate)
        int track_id[2] = {-1, -1};   // part a gate is closed for (-1 = open)
    };
    // events: the picker's pending flap events; belt_mm: current belt travel
    const State &update(const std::vector<FlapEvent> &events, double belt_mm);
    const State &state() const { return st_; }
    uint64_t served = 0;           // parts a gate was closed for

private:
    FlapConfig cfg_;
    State st_;
    double hold_until_[2] = {-1e18, -1e18};
};

}  // namespace des
