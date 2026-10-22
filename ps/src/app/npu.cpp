// npu.cpp — see npu.hpp
#include "npu.hpp"
#include <pthread.h>
#include <sched.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <opencv2/imgproc.hpp>
#include "armnnALUtils/cma_mem_init.hpp"
#include "executor.hpp"
#include "json.hpp"
#include "util.hpp"
namespace des {
namespace {
const char *type_name(armnn::DataType t)
{
    switch (t) {
    case armnn::DataType::Float32: return "float32";
    case armnn::DataType::QAsymmU8: return "uint8 (asym)";
    case armnn::DataType::QAsymmS8: return "int8 (asym)";
    case armnn::DataType::QSymmS8: return "int8 (sym)";
    default: return "other";
    }
}
}  // namespace
NpuClassifier::NpuClassifier() = default;
NpuClassifier::~NpuClassifier()
{
    ex_.reset();   // the network's CMA buffers go before the CMA zone
    zone_.reset();
}
std::string NpuClassifier::load(const std::string &json_path)
{
    nlohmann::json j;
    std::ifstream f(json_path);
    if (!f) return "cannot open " + json_path;
    try { f >> j; } catch (const std::exception &e) { return json_path + ": " + e.what(); }
    std::string model;
    try {
        model = j.at("model").get<std::string>(); classes_ = j.at("classes").get<std::vector<std::string>>();
        const auto &in = j.at("input"); for (int c = 0; c < 3; c++) {
            mean_[c] = in.at("mean").at(c).get<float>(); scale_[c] = in.at("scale").at(c).get<float>();
        }
    } catch (const std::exception &e) {
        return json_path + ": " + e.what();
    }
    if (model.empty() || model[0] != '/') model = dir_of(json_path) + "/" + model;
    if (!cma_mem_is_init()) {
        try { zone_.reset(new CmaResourceZone); } catch (const std::exception &e) { return e.what(); }
    }
    try {
        ex_.reset(new NPUExecutor(model, {"Alnpu", "CpuAcc", "CpuRef"}));
    } catch (const std::exception &e) {
        ex_.reset(); return "load " + model + ": " + e.what();
    }
    if (ex_->GetInputTensorNum() != 1 || ex_->GetOutputTensorNum() != 1) {
        ex_.reset(); return model + ": expected 1 input and 1 output";
    }
    const Size sz = ex_->GetInputAspectRatio(0);
    w_ = sz.m_width;
    h_ = sz.m_height;
    const armnn::DataType it = ex_->GetInputDataType();
    in_u8_ = it == armnn::DataType::QAsymmU8;
    if (!in_u8_ && it != armnn::DataType::Float32) {
        ex_.reset(); return std::string("unsupported input type ") + type_name(it);
    }
    in_scale_ = ex_->GetInputQuantizationScale(0);
    in_zero_ = ex_->GetInputQuantizationOffset(0);
    const armnn::Tensor &ot = ex_->GetOutputTensor(0);
    out_u8_ = ot.GetDataType() == armnn::DataType::QAsymmU8;
    out_n_ = ot.GetNumElements();
    out_scale_ = ex_->GetOutputQuantizationScale(0);
    out_zero_ = ex_->GetOutputQuantizationOffset(0);
    if (out_n_ != classes_.size()) {
        ex_.reset(); return "output has " + std::to_string(out_n_) + " values, json lists " + std::to_string(classes_.size()) +
               " classes";
    }
    return "";
}
std::string NpuClassifier::describe() const
{
    if (!ex_) return "not loaded";
    char b[256];
    snprintf(b, sizeof b, "input %dx%d %s (scale %.6f zero %d), output %zu %s (scale %.6f zero %d)", w_, h_,
             in_u8_ ? "uint8" : "float32", in_scale_, in_zero_, out_n_, out_u8_ ? "uint8" : "float32", out_scale_,
             out_zero_);
    return b;
}
std::vector<float> NpuClassifier::classify(const cv::Mat &crop_bgr, double *pre_ms, double *npu_ms)
{
    if (!ex_ || crop_bgr.empty() || crop_bgr.type() != CV_8UC3) return {};
    const uint64_t t0 = mono_us();
    cv::Mat src = crop_bgr;
    if (src.cols != w_ || src.rows != h_) cv::resize(crop_bgr, src, cv::Size(w_, h_), 0, 0, cv::INTER_AREA);
    CmaBuf &ib = ex_->GetInputBuffer(0);
    const int plane = w_ * h_;
    // NCHW, RGB planes; value = (pixel - mean) * scale, quantized when the input tensor is uint8
    for (int c = 0; c < 3; c++) {
        const int bgr_ch = 2 - c;   // R from channel 2
        if (in_u8_) {
            uint8_t *dst = static_cast<uint8_t *>(ib->virt_addr) + c * plane; uint8_t lut[256];
            for (int v = 0; v < 256; v++) {
                const float x = (v - mean_[c]) * scale_[c]; lut[v] = uint8_t(std::clamp(int(std::lround(x / in_scale_)) + in_zero_, 0, 255));
            }
            for (int y = 0; y < h_; y++) {
                const uint8_t *s = src.ptr<uint8_t>(y); for (int x = 0; x < w_; x++) dst[y * w_ + x] = lut[s[3 * x + bgr_ch]];
            }
        } else {
            float *dst = static_cast<float *>(ib->virt_addr) + c * plane; for (int y = 0; y < h_; y++) {
                const uint8_t *s = src.ptr<uint8_t>(y); for (int x = 0; x < w_; x++) dst[y * w_ + x] = (s[3 * x + bgr_ch] - mean_[c]) * scale_[c];
            }
        }
    }
    ib.sync_device();
    if (pre_ms) *pre_ms = (mono_us() - t0) * 1e-3;
    const uint64_t t1 = mono_us();
    if (!ex_->Run()) return {};
    CmaBuf &ob = ex_->GetOutputBuffer(0);
    ob.sync_cpu();
    if (npu_ms) *npu_ms = (mono_us() - t1) * 1e-3;
    last_logits.resize(out_n_);
    for (size_t i = 0; i < out_n_; i++)
        last_logits[i] = out_u8_ ? (static_cast<uint8_t *>(ob->virt_addr)[i] - out_zero_) * out_scale_
                                 : static_cast<float *>(ob->virt_addr)[i];
    std::vector<float> p(out_n_);
    const float mx = *std::max_element(last_logits.begin(), last_logits.end());
    float sum = 0;
    for (size_t i = 0; i < out_n_; i++) sum += (p[i] = std::exp(last_logits[i] - mx));
    for (float &v : p) v /= sum;
    return p;
}
std::string AsyncClassifier::start(const std::string &json_path, int core)
{
    stop();
    std::string e = npu_.load(json_path);
    if (!e.empty()) return e;
    q_.reopen();
    th_ = std::thread(&AsyncClassifier::loop, this, core);
    return "";
}
bool AsyncClassifier::submit(int id, const cv::Mat &crop, uint64_t frame_us)
{
    if (!th_.joinable() || crop.empty()) return false;
    return q_.push({id, crop, frame_us ? frame_us : mono_us()});
}
bool AsyncClassifier::poll(NpuResult &out)
{
    std::lock_guard<std::mutex> lk(out_m_);
    if (out_.empty()) return false;
    out = out_.front();
    out_.pop_front();
    return true;
}
void AsyncClassifier::stop()
{
    if (!th_.joinable()) return;
    q_.close_and_clear();
    th_.join();
    std::lock_guard<std::mutex> lk(out_m_);
    out_.clear();
}
void AsyncClassifier::loop(int core)
{
    if (core >= 0) (void)pin_to_core(core);
    sched_param sp{};
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
    for (;;) {
        Job job; if (!q_.pop(job)) return;
        double pre = 0, run = 0; std::vector<float> p = npu_.classify(job.crop, &pre, &run);
        last_npu_ms = float(run); if (p.empty()) continue;
        runs++; NpuResult res;
        res.id = job.id; res.n_probs = uint8_t(std::min<size_t>(p.size(), sizeof res.probs / sizeof res.probs[0]));
        for (uint8_t i = 0; i < res.n_probs; i++) res.probs[i] = p[i];
        res.frame_us = job.frame_us; res.pre_ms = float(pre);
        res.npu_ms = float(run); res.lat_ms = (mono_us() - job.frame_us) * 1e-3;
        std::lock_guard<std::mutex> lk(out_m_); out_.push_back(res);
    }
}
}  // namespace des
