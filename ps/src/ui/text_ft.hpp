// text_ft.hpp — Chinese text on cv::Mat with FreeType (OpenCV's Hershey fonts are ASCII only).
// Board: /usr/lib/libfreetype.so.6 (2.12) and the WenQuanYi Zen Hei font that ships with the image
// (/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc, GPL + font exception). Glyphs are rendered once per (char, size) and
// cached, so a panel full of text costs a few blends. Without the font it falls back to cv::putText (ASCII).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>

#include <opencv2/core.hpp>

namespace des {

class TextFT {
public:
    ~TextFT();
    bool open(const std::string &font = "/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc");
    bool ok() const { return face_ != nullptr; }
    // UTF-8 text with its line box's top-left at (x, y), px = font pixel size. Returns the width drawn.
    int draw(cv::Mat &bgr, const std::string &utf8, int x, int y, int px, const cv::Scalar &color);
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

}  // namespace des
