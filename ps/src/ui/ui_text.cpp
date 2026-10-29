// ui_text.cpp — see ui_text.hpp (text_ft + the button/table/chart helpers; geometry, colours and blending unchanged).
#include "ui_text.hpp"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "util.hpp"

namespace des {
namespace {
// One UTF-8 code point, advancing p: bad lead byte skipped, truncated sequence ends the string (as old utf8_decode).
bool next_cp(const unsigned char *&p, const unsigned char *end, uint32_t &cp)
{
    while (p < end) {
        const unsigned char c = *p;
        int n;
        uint32_t v;
        if (c < 0x80) { cp = c; ++p; return true; }
        if ((c >> 5) == 6) { v = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { v = c & 0x0F; n = 3; }
        else if ((c >> 3) == 30) { v = c & 0x07; n = 4; }
        else { ++p; continue; }
        if (p + n > end) return false;
        for (int k = 1; k < n; k++) v = (v << 6) | (p[k] & 0x3F);
        cp = v;
        p += n;
        return true;
    }
    return false;
}
}  // namespace
TextFT::~TextFT()
{
    if (face_) FT_Done_Face(static_cast<FT_Face>(face_));
    if (lib_) FT_Done_FreeType(static_cast<FT_Library>(lib_));
}
bool TextFT::open(const std::string &font)
{
    FT_Library lib;
    if (FT_Init_FreeType(&lib)) return false;
    FT_Face face;
    if (FT_New_Face(lib, font.c_str(), 0, &face)) { FT_Done_FreeType(lib); return false; }
    lib_ = lib;  face_ = face;
    return true;
}
const TextFT::Glyph &TextFT::glyph(uint32_t cp, int px)
{
    auto key = std::make_pair(cp, px);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    Glyph g;
    FT_Face face = static_cast<FT_Face>(face_);
    if (cur_px_ != px) { FT_Set_Pixel_Sizes(face, 0, FT_UInt(px)); cur_px_ = px; }
    if (FT_Load_Char(face, cp, FT_LOAD_RENDER) == 0) {
        const FT_Bitmap &bm = face->glyph->bitmap;
        g.left = face->glyph->bitmap_left;  g.top = face->glyph->bitmap_top;  g.adv = int(face->glyph->advance.x >> 6);
        if (bm.width > 0 && bm.rows > 0)
            g.alpha = cv::Mat(int(bm.rows), int(bm.width), CV_8UC1, bm.buffer, size_t(bm.pitch)).clone();
    }
    if (cache_.size() > 6000) cache_.clear();
    return cache_.emplace(key, std::move(g)).first->second;
}
void TextFT::draw(cv::Mat &img, const std::string &utf8, int x, int y, int px, const cv::Scalar &color)
{
    if (!face_) {   // ASCII fallback
        cv::putText(img, utf8, cv::Point(x, y + px * 3 / 4), cv::FONT_HERSHEY_SIMPLEX, px / 30.0, color, 1);
        return;
    }
    const int baseline = y + px * 7 / 8, nch = img.channels();
    int pen = x;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(utf8.data()), *end = p + utf8.size();
    uint32_t cp;
    while (next_cp(p, end, cp)) {
        const Glyph &g = glyph(cp, px);
        const int gx = pen + g.left, gy = baseline - g.top;
        for (int r = 0; r < g.alpha.rows; r++) {
            const int yy = gy + r;
            if (yy < 0 || yy >= img.rows) continue;
            const uint8_t *a = g.alpha.ptr<uint8_t>(r);
            uint8_t *d = img.ptr<uint8_t>(yy);
            for (int c = 0; c < g.alpha.cols; c++) {
                const int xx = gx + c, al = a[c];
                if (xx < 0 || xx >= img.cols || al == 0) continue;
                for (int k = 0; k < std::min(nch, 3); k++) {
                    uint8_t &v = d[xx * nch + k];
                    v = uint8_t((v * (255 - al) + int(color[k]) * al) / 255);
                }
            }
        }
        pen += g.adv;
    }
}
void TextFT::draw_solid(cv::Mat &img, const std::string &utf8, int x, int y, int px, const cv::Scalar &color)
{
    if (!face_) {   // ASCII fallback (Hershey, also write-only)
        cv::putText(img, utf8, cv::Point(x, y + px * 3 / 4), cv::FONT_HERSHEY_SIMPLEX, px / 30.0, color, 1);
        return;
    }
    const int baseline = y + px * 7 / 8, nch = img.channels();
    const uint8_t b = uint8_t(color[0]), g = uint8_t(color[1]), r = uint8_t(color[2]);
    int pen = x;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(utf8.data()), *end = p + utf8.size();
    uint32_t cp;
    while (next_cp(p, end, cp)) {
        const Glyph &gl = glyph(cp, px);
        const int gx = pen + gl.left, gy = baseline - gl.top;
        for (int row = 0; row < gl.alpha.rows; row++) {
            const int yy = gy + row;
            if (yy < 0 || yy >= img.rows) continue;
            const uint8_t *a = gl.alpha.ptr<uint8_t>(row);
            uint8_t *d = img.ptr<uint8_t>(yy);
            for (int c = 0; c < gl.alpha.cols; c++) {
                const int xx = gx + c;
                if (xx < 0 || xx >= img.cols || a[c] < 100) continue;   // ~40 % coverage: CJK strokes stay whole
                uint8_t *q = d + xx * nch;
                q[0] = b; q[1] = g; q[2] = r;
            }
        }
        pen += gl.adv;
    }
}
int TextFT::width(const std::string &utf8, int px)
{
    if (!face_) { int base = 0; return cv::getTextSize(utf8, cv::FONT_HERSHEY_SIMPLEX, px / 30.0, 1, &base).width; }
    int w = 0;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(utf8.data()), *end = p + utf8.size();
    uint32_t cp;
    while (next_cp(p, end, cp)) w += glyph(cp, px).adv;
    return w;
}
namespace ui {
void Canvas::button(const cv::Rect &r, const std::string &label, bool on, int action, int arg, int px)
{
    cv::rectangle(img_, r, on ? ACCENT : PANEL, cv::FILLED);
    cv::rectangle(img_, r, LINE, 1);
    const int w = txt_.width(label, px);
    // action 0 = not available right now (line running, locked page...): grey label, no click
    text(label, r.x + (r.width - w) / 2, r.y + (r.height - px) / 2 - 1, px, on ? ON_TEXT : action ? TEXT : DIM);
    if (action) hits_.push_back({r, action, arg});
}
void Canvas::header_row(const char *const *hd, const int *cx, int n, int y, int px)
{
    for (int i = 0; i < n; i++) text(hd[i], cx[i], y, px, DIM);
}
void Canvas::stepper(int y, const std::string &value, int dec_action, int inc_action, int arg, const char *unit)
{
    button({470, y, 46, 28}, "-", false, dec_action, arg, 26);   // U+2212 is not in the font
    centered(value, 520, 186, y + 2, 22, VALUE);
    button({710, y, 46, 28}, "+", false, inc_action, arg, 26);
    text(unit, 772, y + 6, 16, DIM);
}
void Canvas::wide_button(int y, const std::string &value, bool on, int action, int px)
{
    button({470, y, 286, 28}, value, on, action, 0, px);
}
void Canvas::row(const cv::Rect &r, bool selected, int action, int arg)
{
    if (selected) cv::rectangle(img_, r, SEL, cv::FILLED);
    if (action) hits_.push_back({r, action, arg});
}
void size_chart(Canvas &c, const cv::Rect &r, const char *title, const Trend *v, int n, int cap, float nom, float tol)
{
    cv::Mat &img = c.img();
    cv::rectangle(img, r, PANEL, cv::FILLED);
    double m = 0, sd = 0;   // Trend is {v, ng}: stats1d() would need a copy; two plain passes instead
    for (int i = 0; i < n; i++) m += v[i].v;
    if (n) m /= n;
    for (int i = 0; i < n; i++) sd += (v[i].v - m) * (v[i].v - m);
    sd = n > 1 ? std::sqrt(sd / (n - 1)) : 0;
    const double cpk = sd > 1e-9 ? std::min(nom + tol - m, m - (nom - tol)) / (3 * sd) : 0;
    c.text(title, r.x + 10, r.y + 6, 18, TEXT);
    c.text(n == 0 ? std::string("暂无测量")
                  : des::fmt("均值 %.3f", m) + des::fmt("  σ %.3f", sd) + des::fmt("  Cpk %.2f", cpk) +
                        des::fmt("  n=%.0f", double(n)),
           r.x + 10, r.y + 30, 15, n == 0 ? DIM : cpk >= 1.33 ? GREEN : cpk >= 1.0 ? ORANGE : RED);
    const cv::Rect pl(r.x + 58, r.y + 58, r.width - 70, r.height - 70);
    double span = 2.5 * tol;
    for (int i = 0; i < n; i++) span = std::max(span, 1.2 * std::fabs(v[i].v - nom));
    auto yy = [&](double val) { return int(pl.y + pl.height / 2 - (val - nom) / span * (pl.height / 2)); };
    for (double lv : {nom - tol, nom, nom + tol}) {
        const int y = yy(lv);
        cv::line(img, {pl.x, y}, {pl.x + pl.width, y}, lv == nom ? DIM : RED, 1);
        c.text(des::fmt("%.2f", lv), r.x + 6, y - 9, 14, lv == nom ? DIM : RED);
    }
    cv::Point prev(-1, -1);
    for (int i = 0; i < n; i++) {
        const cv::Point p(pl.x + int(i * (pl.width - 1) / cap), std::clamp(yy(v[i].v), pl.y, pl.y + pl.height));
        if (prev.x >= 0) cv::line(img, prev, p, INFO, 1, cv::LINE_AA);
        cv::circle(img, p, 3, v[i].ng ? RED : GREEN, cv::FILLED, cv::LINE_AA);
        prev = p;
    }
}
void timing_chart(Canvas &c, const cv::Rect &r, const Sample *s, int n, int cap)
{
    cv::Mat &img = c.img();
    cv::rectangle(img, r, PANEL, cv::FILLED);
    c.text("近 60 秒：", r.x + 10, r.y + 6, 18, TEXT);
    c.text("工作线程 ms", r.x + 110, r.y + 8, 15, INFO);
    c.text("NPU ms", r.x + 220, r.y + 8, 15, VALUE);
    c.text("带速 mm/s", r.x + 300, r.y + 8, 15, TEXT);
    bool any_enc = false;
    float mt = 20, mb = 100;
    for (int i = 0; i < n; i++) {
        any_enc = any_enc || s[i].belt_enc >= 0;
        mt = std::max({mt, s[i].worker, s[i].npu});
        mb = std::max({mb, s[i].belt, s[i].belt_enc});
    }
    if (any_enc) c.text("编码器实测", r.x + 390, r.y + 8, 15, GREEN);
    const cv::Rect pl(r.x + 40, r.y + 40, r.width - 90, r.height - 60);
    for (int k = 0; k <= 4; k++) {
        const int y = pl.y + pl.height - k * pl.height / 4;
        cv::line(img, {pl.x, y}, {pl.x + pl.width, y}, LINE, 1);
        c.text(des::fmt("%.0f", mt * k / 4), r.x + 6, y - 8, 13, INFO);
        c.text(des::fmt("%.0f", mb * k / 4), pl.x + pl.width + 6, y - 8, 13, TEXT);
    }
    auto plot = [&](int which, const cv::Scalar &col, float scale) {
        cv::Point prev(-1, -1);
        for (int i = 0; i < n; i++) {
            const float v = which == 0 ? s[i].worker : which == 1 ? s[i].npu : which == 2 ? s[i].belt : s[i].belt_enc;
            if (v < 0) {   // no encoder in this sample: gap
                prev = {-1, -1};
                continue;
            }
            const cv::Point p(pl.x + int(i * (pl.width - 1) / cap), pl.y + pl.height - int(v / scale * pl.height));
            if (prev.x >= 0) cv::line(img, prev, p, col, 1, cv::LINE_AA);
            prev = p;
        }
    };
    plot(2, TEXT, mb);
    if (any_enc) plot(3, GREEN, mb);
    plot(0, INFO, mt);
    plot(1, VALUE, mt);
}
}  // namespace ui
}  // namespace des
