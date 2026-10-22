// flat_field.cpp — see flat_field.hpp
#include "flat_field.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <opencv2/imgproc.hpp>
#include "json.hpp"
namespace des {
float FlatField::white_at(float x, float y) const
{
    // grid coordinates of the point: cell centres sit at integer positions
    float gx = (x + 0.5f) * gw / width - 0.5f, gy = (y + 0.5f) * gh / height - 0.5f;
    gx = std::min(std::max(gx, 0.f), float(gw - 1));
    gy = std::min(std::max(gy, 0.f), float(gh - 1));
    const int x0 = std::min(int(gx), gw - 2), y0 = std::min(int(gy), gh - 2);
    const float fx = gx - x0, fy = gy - y0;
    const float *r0 = &white[size_t(y0) * gw], *r1 = r0 + gw;
    return (1 - fy) * ((1 - fx) * r0[x0] + fx * r0[x0 + 1]) + fy * ((1 - fx) * r1[x0] + fx * r1[x0 + 1]);
}
float FlatField::gain_at(float x, float y) const
{
    const float w = white_at(x, y);
    const float g = w > 0 ? ref / w : max_gain;
    return std::min(std::max(g, 1.f / max_gain), max_gain);
}

std::string load_flat(const std::string &path, FlatField &f)
{
    std::ifstream in(path);
    if (!in) return "cannot open " + path;
    try {
        nlohmann::json j;
        in >> j;
        FlatField g;
        g.gw = j.at("grid_w").get<int>(); g.gh = j.at("grid_h").get<int>();
        if (j.contains("width")) g.width = j["width"].get<int>();
        if (j.contains("height")) g.height = j["height"].get<int>();
        g.ref = j.at("ref").get<float>();
        if (j.contains("max_gain")) g.max_gain = j["max_gain"].get<float>();
        g.white = j.at("white").get<std::vector<float>>();
        if (!g.ok()) return path + ": grid size / white levels do not match";
        f = g;
    } catch (const std::exception &e) {
        return path + ": " + e.what();
    }
    return "";
}

std::string save_flat(const std::string &path, const FlatField &f, const std::string &note)
{
    nlohmann::ordered_json j;
    j["comment"] = "des_tool calib-flat 生成（白纸随皮带走过视野）：视野白电平网格，零件裁切图乘 ref / white。"
                   "换相机 / 灯光 / 曝光后重做。";
    if (!note.empty()) j["note"] = note;
    j["grid_w"] = f.gw; j["grid_h"] = f.gh;
    j["width"] = f.width; j["height"] = f.height;
    j["ref"] = std::round(f.ref * 100) / 100;
    j["max_gain"] = f.max_gain;
    nlohmann::json w = nlohmann::json::array();
    for (float v : f.white) w.push_back(std::round(v * 100) / 100);
    j["white"] = w;
    std::ofstream out(path);
    if (!out) return "cannot write " + path;
    out << j.dump(1) << "\n";
    return out ? "" : "write failed: " + path;
}

void apply_flat(cv::Mat &crop, const FlatField &f, const double M[9])
{
    if (crop.empty() || crop.type() != CV_8UC3 || !f.ok()) return;
    const int N = std::max(crop.cols, crop.rows), G = std::max(2, N / 8);
    static thread_local cv::Mat g, gf, g3, fl;   // per-thread scratch: same pixels, no per-crop allocation
    g.create(G, G, CV_32F);                      // every cell is written below
    for (int j = 0; j < G; j++)
        for (int i = 0; i < G; i++) {
            const double u = (i + 0.5) * crop.cols / G - 0.5, v = (j + 0.5) * crop.rows / G - 0.5;
            const double w = M[6] * u + M[7] * v + M[8];
            const double x = (M[0] * u + M[1] * v + M[2]) / w, y = (M[3] * u + M[4] * v + M[5]) / w;
            g.at<float>(j, i) = f.gain_at(float(x), float(y));
        }
    cv::resize(g, gf, crop.size(), 0, 0, cv::INTER_LINEAR);
    const cv::Mat mv[3] = {gf, gf, gf};
    cv::merge(mv, 3, g3);
    crop.convertTo(fl, CV_32FC3);
    cv::multiply(fl, g3, fl);
    fl.convertTo(crop, CV_8UC3);   // saturating
}
std::string FlatBuilder::build(FlatField &out, int gw, int gh, FlatStats *st) const
{
    if (frames_.size() < 2) return "need at least 2 frames of the white sheet";
    const cv::Size sz = frames_[0].size();
    for (const cv::Mat &m : frames_)
        if (m.size() != sz || m.type() != CV_8UC1) return "frames differ in size or type";
    cv::Mat mx = frames_[0].clone(), mn = frames_[0].clone();
    for (size_t k = 1; k < frames_.size(); k++) { cv::max(mx, frames_[k], mx); cv::min(mn, frames_[k], mn); }
    cv::Mat range;
    cv::subtract(mx, mn, range);
    cv::Mat acc = cv::Mat::zeros(sz, CV_32F), cnt = cv::Mat::zeros(sz, CV_32F);
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(51, 51));
    std::vector<cv::Mat> masks;
    for (const cv::Mat &fr : frames_) {
        // sheet pixel: above 60 % of its own min..max range, with a real contrast (> 25 gray) between belt and sheet,
        cv::Mat lvl, mask, ok;
        cv::addWeighted(mn, 0.4, mx, 0.6, 0, lvl, CV_8U);
        cv::compare(fr, lvl, mask, cv::CMP_GT);
        cv::compare(range, 25, ok, cv::CMP_GT);
        mask &= ok;
        cv::erode(mask, mask, kernel);
        cv::Mat f32;
        fr.convertTo(f32, CV_32F);
        cv::accumulate(f32, acc, mask);
        cv::accumulate(cv::Mat(sz, CV_32F, cv::Scalar(1)), cnt, mask);
        masks.push_back(mask);
    }
    cv::Mat have = cnt > 0;
    const double covered = double(cv::countNonZero(have)) / have.total();
    if (covered < 0.5) return "the sheet covered only " + std::to_string(int(covered * 100)) + " % of the view";
    cv::Mat white;
    cv::divide(acc, cv::max(cnt, 1.f), white);
    {   // fill uncovered pixels (view border the sheet never reached) from their neighbourhood
        cv::Mat hf, wv, wn;
        have.convertTo(hf, CV_32F, 1.0 / 255);
        cv::GaussianBlur(white.mul(hf), wv, cv::Size(), 40);
        cv::GaussianBlur(hf, wn, cv::Size(), 40);
        cv::Mat fill = wv / cv::max(wn, 1e-6f);
        fill.copyTo(white, ~have);
    }
    cv::GaussianBlur(white, white, cv::Size(), 12);
    cv::Mat grid;
    cv::resize(white, grid, cv::Size(gw, gh), 0, 0, cv::INTER_AREA);
    // reference = median of the smoothed white level in the central 10 % x 10 % of the view
    cv::Mat c = white(cv::Rect(sz.width * 45 / 100, sz.height * 45 / 100, sz.width / 10, sz.height / 10)).clone();
    std::vector<float> cv_(c.begin<float>(), c.end<float>());
    std::nth_element(cv_.begin(), cv_.begin() + cv_.size() / 2, cv_.end());
    FlatField f;
    f.gw = gw; f.gh = gh; f.width = sz.width; f.height = sz.height;
    f.ref = cv_[cv_.size() / 2];
    f.white.assign(grid.begin<float>(), grid.end<float>());
    if (!f.ok()) return "white level map invalid (reference " + std::to_string(f.ref) + ")";
    if (st) {
        st->frames = int(frames_.size()); st->covered = float(covered); st->centre = f.ref;
        double mnv, mxv; cv::minMaxLoc(white, &mnv, &mxv);
        st->min_rel = float(mnv / f.ref);
        const float W = float(sz.width - 1), H = float(sz.height - 1);
        const float cx[4] = {0, W, 0, W}, cy[4] = {0, 0, H, H};
        for (int i = 0; i < 4; i++) st->corner_rel[i] = f.white_at(cx[i], cy[i]) / f.ref;
        std::vector<float> r;   // residual: every frame's sheet pixels after correction (every 4th pixel)
        for (size_t k = 0; k < frames_.size(); k++)
            for (int y = 0; y < sz.height; y += 4) {
                const uint8_t *p = frames_[k].ptr<uint8_t>(y), *m = masks[k].ptr<uint8_t>(y);
                for (int x = 0; x < sz.width; x += 4)
                    if (m[x]) r.push_back(p[x] * f.gain_at(float(x), float(y)) / f.ref);
            }
        if (!r.empty()) {
            std::sort(r.begin(), r.end());
            st->resid_p5 = r[r.size() * 5 / 100]; st->resid_p95 = r[r.size() * 95 / 100];
        }
    }
    out = f;
    return "";
}
}  // namespace des
