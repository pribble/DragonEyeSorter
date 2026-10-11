// npu_cls.hpp — part classifier on the NPU: one part crop (roi_crop.hpp) -> probabilities of the 8 classes.
//
// Model = train/export_npu.py output: <name>_uint8.onnx (QDQ / UINT8 / per-tensor, the vendor fire_detect format)
// + <name>.json (class names in id order, input normalization). Loaded through the vendor ArmNN executor
// (ps/vision/executor.cpp, MIT) on backends Alnpu, CpuAcc, CpuRef. The ArmNN ONNX parser folds the input
// QuantizeLinear into the input tensor, so the input is written as uint8 with the input tensor's scale / zero point
// (same as the vendor demo); float inputs are handled too. Softmax runs here (it is cut off the exported graph).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

class NPUExecutor;
class CmaResourceZone;

namespace des {

class NpuClassifier {
public:
    NpuClassifier();
    ~NpuClassifier();
    std::string load(const std::string &json_path);   // "" on success
    bool loaded() const { return ex_ != nullptr; }

    // crop: BGR (resized if not the input size). Returns softmax probabilities (empty on failure).
    // pre_ms / npu_ms: time spent preparing the input / running the network.
    std::vector<float> classify(const cv::Mat &crop_bgr, double *pre_ms = nullptr, double *npu_ms = nullptr);
    std::vector<float> last_logits;                     // dequantized network output of the last call

    const std::vector<std::string> &classes() const { return classes_; }
    int input_size() const { return w_; }
    std::string describe() const;                       // input / output tensor types, for logs

private:
    std::unique_ptr<CmaResourceZone> zone_;
    std::unique_ptr<NPUExecutor> ex_;
    std::vector<std::string> classes_;
    int w_ = 0, h_ = 0;
    float mean_[3] = {0, 0, 0}, scale_[3] = {1 / 255.f, 1 / 255.f, 1 / 255.f};
    bool in_u8_ = false, out_u8_ = false;
    float in_scale_ = 1, out_scale_ = 1;
    int in_zero_ = 0, out_zero_ = 0;
    size_t out_n_ = 0;
};

}  // namespace des
