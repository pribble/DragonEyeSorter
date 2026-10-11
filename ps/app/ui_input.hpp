// ui_input.hpp — mouse / keyboard / touch screen for the HDMI HMI through evdev (/dev/input/event*), with hot plug
// (the directory is rescanned every 2 s). Relative mice move a cursor kept here; absolute devices (touch screens) are
// scaled to the screen with their EVIOCGABS ranges. A background thread reads; the HMI takes events per cycle.
// The board kernel has usbhid + hid-generic and the input class; nothing is plugged in by default (2026-10-11).
// Scripted input: lines "click X Y" / "move X Y" / "wheel N" / "key CODE" / "snap" written to the FIFO /tmp/des_ui.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace des {

struct UiEvent {
    enum Kind { Click, Wheel, Key } kind = Click;
    int x = 0, y = 0;    // screen position (Click / Wheel)
    int value = 0;       // Wheel: +1 / -1; Key: linux key code
};

class UiInput {
public:
    UiInput(int screen_w, int screen_h) : w_(screen_w), h_(screen_h), x_(screen_w / 2), y_(screen_h / 2) {}
    ~UiInput() { stop(); }
    void start();
    void stop();
    std::vector<UiEvent> take();
    // cursor position; visible = some pointer moved / clicked in the last 10 s
    void cursor(int &x, int &y, bool &visible) const;
    int devices() const { return ndev_; }
    bool take_snap() { return snap_.exchange(false); }   // FIFO "snap": save the next displayed frame (tests)

private:
    void loop();
    void fifo_line(const std::string &line);
    int w_, h_;
    std::atomic<int> x_, y_;
    std::atomic<double> last_move_{-1e9};
    std::atomic<int> ndev_{0};
    std::atomic<bool> quit_{false};
    std::atomic<bool> snap_{false};
    std::thread th_;
    mutable std::mutex m_;
    std::vector<UiEvent> ev_;
};

}  // namespace des
