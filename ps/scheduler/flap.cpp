// flap.cpp — see flap.hpp
#include "flap.hpp"

#include <algorithm>

namespace des {

void FlapSequencer::set_for(int outlet)
{
    st_.outlet = outlet;
    st_.us[0] = outlet == 0 ? cfg_.f1_pass_us : cfg_.f1_ng_us;
    st_.us[1] = outlet == 2 ? cfg_.f2_rework_us : cfg_.f2_scrap_us;
}

const FlapSequencer::State &FlapSequencer::update(const std::vector<FlapEvent> &events, double belt_mm)
{
    if (st_.track_id < 0 && st_.us[0] == 0) set_for(0);   // first call: resting = recheck path
    // the current part is still passing: keep the flaps
    if (st_.track_id >= 0 && belt_mm < hold_until_) return st_;
    if (st_.track_id >= 0) {   // passed
        done_.push_back(st_.track_id);
        if (done_.size() > 64) done_.erase(done_.begin());
        st_.track_id = -1;
        set_for(0);
    }
    // next event not served yet, within prepare_mm
    for (const FlapEvent &e : events) {
        if (std::find(done_.begin(), done_.end(), e.track_id) != done_.end()) continue;
        if (belt_mm >= e.belt_mm - cfg_.prepare_mm) {
            st_.track_id = e.track_id;
            set_for(e.outlet);
            hold_until_ = e.belt_mm + cfg_.clear_mm;
            served++;
        }
        break;   // events are ordered: only the first pending one can be due
    }
    return st_;
}

}  // namespace des
