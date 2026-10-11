// text_ft.cpp — see text_ft.hpp
#include "text_ft.hpp"

#include <algorithm>
#include <vector>

#include <opencv2/imgproc.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace des {
namespace {

std::vector<uint32_t> utf8_decode(const std::string &s)
{
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        uint32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
        else { i++; continue; }
        if (i + n > s.size()) break;
        for (int k = 1; k < n; k++) cp = (cp << 6) | (s[i + k] & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
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
    if (FT_New_Face(lib, font.c_str(), 0, &face)) {
        FT_Done_FreeType(lib);
        return false;
    }
    lib_ = lib;
    face_ = face;
    return true;
}

const TextFT::Glyph &TextFT::glyph(uint32_t cp, int px)
{
    auto key = std::make_pair(cp, px);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    Glyph g;
    FT_Face face = static_cast<FT_Face>(face_);
    if (cur_px_ != px) {
        FT_Set_Pixel_Sizes(face, 0, FT_UInt(px));
        cur_px_ = px;
    }
    if (FT_Load_Char(face, cp, FT_LOAD_RENDER) == 0) {
        const FT_Bitmap &bm = face->glyph->bitmap;
        g.left = face->glyph->bitmap_left;
        g.top = face->glyph->bitmap_top;
        g.adv = int(face->glyph->advance.x >> 6);
        if (bm.width > 0 && bm.rows > 0)
            g.alpha = cv::Mat(int(bm.rows), int(bm.width), CV_8UC1, bm.buffer, size_t(bm.pitch)).clone();
    }
    if (cache_.size() > 6000) cache_.clear();
    return cache_.emplace(key, std::move(g)).first->second;
}

int TextFT::draw(cv::Mat &img, const std::string &utf8, int x, int y, int px, const cv::Scalar &color)
{
    if (!face_) {   // ASCII fallback
        cv::putText(img, utf8, cv::Point(x, y + px * 3 / 4), cv::FONT_HERSHEY_SIMPLEX, px / 30.0, color, 1);
        int base = 0;
        return cv::getTextSize(utf8, cv::FONT_HERSHEY_SIMPLEX, px / 30.0, 1, &base).width;
    }
    const int baseline = y + px * 7 / 8;
    int pen = x;
    const int nch = img.channels();
    for (uint32_t cp : utf8_decode(utf8)) {
        const Glyph &g = glyph(cp, px);
        const int gx = pen + g.left, gy = baseline - g.top;
        for (int r = 0; r < g.alpha.rows; r++) {
            const int yy = gy + r;
            if (yy < 0 || yy >= img.rows) continue;
            const uint8_t *a = g.alpha.ptr<uint8_t>(r);
            uint8_t *d = img.ptr<uint8_t>(yy);
            for (int c = 0; c < g.alpha.cols; c++) {
                const int xx = gx + c;
                if (xx < 0 || xx >= img.cols || a[c] == 0) continue;
                const int al = a[c];
                for (int k = 0; k < std::min(nch, 3); k++) {
                    uint8_t &v = d[xx * nch + k];
                    v = uint8_t((v * (255 - al) + int(color[k]) * al) / 255);
                }
            }
        }
        pen += g.adv;
    }
    return pen - x;
}

int TextFT::width(const std::string &utf8, int px)
{
    if (!face_) {
        int base = 0;
        return cv::getTextSize(utf8, cv::FONT_HERSHEY_SIMPLEX, px / 30.0, 1, &base).width;
    }
    int w = 0;
    for (uint32_t cp : utf8_decode(utf8)) w += glyph(cp, px).adv;
    return w;
}

}  // namespace des
