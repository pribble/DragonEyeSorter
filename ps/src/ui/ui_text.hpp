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
inline const cv::Scalar BG(34, 31, 29), PANEL(52, 48, 45), LINE(80, 76, 72), WHITE(235, 235, 235),
    DIM(150, 150, 150), GREEN(90, 200, 90), RED(80, 80, 235), ORANGE(40, 165, 255), CYAN(220, 200, 70),
    YELLOW(70, 215, 235), ACCENT(225, 150, 60);
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
