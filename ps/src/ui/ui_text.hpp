// ui_text.hpp — Chinese text with FreeType + shared drawing helpers for the HDMI HMI (was text_ft.* + hmi.cpp's inline
// canvas / button / table / chart code).
//
// TextFT: board font wqy-zenhei.ttc (GPL + font exception); glyphs cached per (char, size), cv::putText ASCII fallback.
// Unchanged from text_ft.cpp: origin = line box top-left, baseline y + px*7/8, helpers reproduce the old pixels.
//
// Canvas = (image, font, hit list); its helpers register click hits like the old hmi.cpp code.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>

namespace des {

namespace ui {
// Light theme (2026-10-30, user: white interface). BGR. Named by use, not by hue: TEXT = normal text, VALUE = numbers the
// operator reads / sets, INFO = secondary data, ON_TEXT = text on a filled colour (on buttons, state bars). On a white
// page the status colours are darker than they were on the old dark theme so they keep their contrast.
inline const cv::Scalar BG(250, 250, 250), PANEL(236, 234, 232), LINE(196, 192, 188), TEXT(40, 36, 33),
    DIM(118, 114, 110), GREEN(40, 140, 40), RED(40, 40, 200), ORANGE(0, 120, 225), INFO(150, 110, 0),
    VALUE(170, 80, 20), ACCENT(200, 120, 40), ON_TEXT(255, 255, 255), SEL(250, 228, 205);
struct Hit {
    cv::Rect box;
    int action = 0, arg = 0;
};
}  // namespace ui

class TextFT {
public:
    ~TextFT();
    bool open(const std::string &font = "/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc");
    bool ok() const { return face_ != nullptr; }
    // UTF-8 text, line box top-left at (x, y), px = font pixel size
    void draw(cv::Mat &bgr, const std::string &utf8, int x, int y, int px, const cv::Scalar &color);
    // Same, but only writes: pixels whose coverage passes a threshold get the colour, nothing is read back. For the
    // video overlay drawn straight into the display buffer, which is not cached (a read per pixel would cost
    // milliseconds per label at 60 fps); the edges are hard, like the Hershey text it replaces.
    void draw_solid(cv::Mat &bgr, const std::string &utf8, int x, int y, int px, const cv::Scalar &color);
    int width(const std::string &utf8, int px);

private:
    struct Glyph {
        cv::Mat alpha;   // 8-bit coverage
        int left = 0, top = 0, adv = 0;
    };
    const Glyph &glyph(uint32_t cp, int px);
    void *lib_ = nullptr, *face_ = nullptr;   // FT_Library / FT_Face
    int cur_px_ = 0;
    std::map<std::pair<uint32_t, int>, Glyph> cache_;
};

namespace ui {

class Canvas {
public:
    Canvas(cv::Mat &img, TextFT &txt, std::vector<Hit> &hits) : img_(img), txt_(txt), hits_(hits) {}
    cv::Mat &img() { return img_; }
    void text(const std::string &s, int x, int y, int px, const cv::Scalar &c) { txt_.draw(img_, s, x, y, px, c); }
    int width(const std::string &s, int px) { return txt_.width(s, px); }
    void centered(const std::string &s, int x, int w, int y, int px, const cv::Scalar &c)   // line box centred in [x,w)
    {
        text(s, x + (w - width(s, px)) / 2, y, px, c);
    }
    void button(const cv::Rect &r, const std::string &label, bool on, int action, int arg, int px = 22);
    void header_row(const char *const *hd, const int *cx, int n, int y, int px = 16);   // hd[i] at cx[i], DIM
    // params row (label drawn by the caller): [-] (470,y,46x28), value centred in [520,706), [+] (710,y), unit (772,y+6)
    void stepper(int y, const std::string &value, int dec_action, int inc_action, int arg, const char *unit);
    void wide_button(int y, const std::string &value, bool on, int action, int px = 20);   // (470, y, 286x28)
    void row(const cv::Rect &r, bool selected, int action, int arg);                       // table row + click hit

private:
    cv::Mat &img_;
    TextFT &txt_;
    std::vector<Hit> &hits_;
};

struct Trend {
    float v = 0;   // measured value
    bool ng = false;
};
struct Sample {
    float worker = 0, npu = 0, belt = 0;
    float belt_enc = -1;   // encoder-measured belt speed, < 0 = no encoder in this sample
};

// size trend with nominal / tolerance lines and mean, sigma, Cpk; cap = x divisor (79 for the 80-value ring)
void size_chart(Canvas &c, const cv::Rect &r, const char *title, const Trend *v, int n, int cap, float nom, float tol);
// last-60-s panel: worker / NPU ms + belt speed (stepper and encoder), same legend keys as before
void timing_chart(Canvas &c, const cv::Rect &r, const Sample *s, int n, int cap);

}  // namespace ui
}  // namespace des
