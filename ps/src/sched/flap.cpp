// flap.cpp — see flap.hpp
#include "flap.hpp"

#include <algorithm>

namespace des {

const FlapSequencer::State &FlapSequencer::update(const std::vector<FlapEvent> &events, double belt_mm)
{
    const int n = int(std::min<size_t>(cfg_.gates.size(), 2));
    for (int g = 0; g < n; g++) {
        // closed for a part: stays closed until that part is clear_mm past the contact point, even if its event
        // vanished meanwhile (the track was retired, re-decided, ...)
        if (st_.track_id[g] >= 0 && belt_mm >= hold_until_[g]) st_.track_id[g] = -1;
        if (st_.track_id[g] < 0)
            for (const FlapEvent &e : events) {
                if (e.gate != g) continue;
                if (belt_mm >= e.belt_mm - cfg_.prepare_mm && belt_mm < e.belt_mm + cfg_.clear_mm) {
                    st_.track_id[g] = e.track_id;
                    hold_until_[g] = e.belt_mm + cfg_.clear_mm;
                    served++;
                    break;
                }
            }
        st_.us[g] = st_.track_id[g] >= 0 ? cfg_.gates[g].closed_us : cfg_.gates[g].open_us;
    }
    return st_;
}

}  // namespace des
