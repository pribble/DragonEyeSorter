// test_npu: the part classifier on the board's NPU against the PC reference (train/eval_onnx.py --refs).
//   ./test_npu <model.json> <refs_dir> [repeats]
// For every reference crop: NPU top-1 vs the label and vs onnxruntime (fp32 and uint8 on the PC), max |logit diff|
// to the uint8 reference; then latency over `repeats` runs (test report: AI 推理耗时). Exit code = failed checks.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>

#include "json.hpp"
#include "npu.hpp"

using namespace des;

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: test_npu <model.json> <refs_dir> [repeats]\n");
        return 1;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);   // a crash inside ArmNN must not swallow the log
    const std::string dir = argv[2];
    const int repeats = argc > 3 ? atoi(argv[3]) : 100;
    NpuClassifier npu;
    auto t0 = std::chrono::steady_clock::now();
    std::string err = npu.load(argv[1]);
    double load_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!err.empty()) {
        fprintf(stderr, "load: %s\n", err.c_str());
        return 1;
    }
    printf("loaded in %.0f ms: %s\n", load_ms, npu.describe().c_str());

    nlohmann::json j;
    {
        std::ifstream f(dir + "/refs.json");
        if (!f) { fprintf(stderr, "no %s/refs.json\n", dir.c_str()); return 1; }
        f >> j;
    }
    int n = 0, ok_label = 0, ok_fp32 = 0, ok_u8 = 0;
    double worst_u8 = 0;
    cv::Mat first;
    for (auto it = j["refs"].begin(); it != j["refs"].end(); ++it) {
        cv::Mat img = cv::imread(dir + "/" + it.key(), cv::IMREAD_COLOR);
        if (img.empty()) { fprintf(stderr, "cannot read %s\n", it.key().c_str()); continue; }
        if (first.empty()) first = img;
        std::vector<float> p = npu.classify(img);
        if (p.empty()) { fprintf(stderr, "inference failed on %s\n", it.key().c_str()); return 1; }
        const int top = int(std::max_element(p.begin(), p.end()) - p.begin());
        auto argmax = [](const nlohmann::json &v) {
            int b = 0;
            for (size_t i = 1; i < v.size(); i++) if (v[i].get<float>() > v[b].get<float>()) b = int(i);
            return b;
        };
        const auto &r = it.value();
        double d = 0;
        for (size_t i = 0; i < npu.last_logits.size(); i++)
            d = std::max(d, double(std::fabs(npu.last_logits[i] - r["uint8"][i].get<float>())));
        worst_u8 = std::max(worst_u8, d);
        n++;
        ok_label += top == r["label"].get<int>();
        ok_fp32 += top == argmax(r["fp32"]);
        nlohmann::json u8c = r["uint8"];   // raw PC uint8 logits + the model's logit_offset (what the board adds)
        for (size_t i = 0; i < u8c.size() && i < npu.logit_offset().size(); i++)
            u8c[i] = u8c[i].get<float>() + npu.logit_offset()[i];
        ok_u8 += top == argmax(u8c);
        if (n <= 8)
            printf("  %-28s label %d npu %d (p %.3f)  pc fp32 %d uint8 %d  max|dlogit| vs uint8 %.3f\n", it.key().c_str(),
                   r["label"].get<int>(), top, p[top], argmax(r["fp32"]), argmax(u8c), d);
    }
    printf("== %d crops: NPU = label %d, = PC fp32 %d, = PC uint8 %d; max |logit - PC uint8| %.3f\n", n, ok_label,
           ok_fp32, ok_u8, worst_u8);

    // accuracy ON THE NPU over a whole val tree (<dir>/<id>_<label>/*.png|jpg): the number for the test report
    if (const char *ev = getenv("NPU_EVAL_DIR")) {
        const int nc = int(npu.classes().size());
        std::vector<int> conf(size_t(nc * nc), 0);
        std::vector<cv::String> files;
        cv::glob(std::string(ev) + "/*", files, true);
        int total = 0;
        // NPU_DUMP=<csv>: one line per crop, "<class dir>/<file>,<class id>,<raw logit 0>,..." (the dequantized
        // network output, before logit_offset): for fitting the bias correction against the board's NPU itself
        // (train/npu_bias.py), whose integer arithmetic differs from the PC's uint8 simulation by up to ~1.7
        FILE *dump = getenv("NPU_DUMP") ? fopen(getenv("NPU_DUMP"), "w") : nullptr;
        for (const cv::String &f : files) {
            size_t a = f.find_last_of('/'), b = f.find_last_of('/', a - 1);
            std::string cls_dir = f.substr(b + 1, a - b - 1);
            int cid = atoi(cls_dir.c_str());
            if (cid < 0 || cid >= nc || !isdigit((unsigned char)cls_dir[0])) continue;
            cv::Mat img = cv::imread(f, cv::IMREAD_COLOR);
            if (img.empty()) continue;
            std::vector<float> p = npu.classify(img);
            if (p.empty()) continue;
            conf[size_t(cid * nc + int(std::max_element(p.begin(), p.end()) - p.begin()))]++;
            if (dump) {
                fprintf(dump, "%s/%s,%d", cls_dir.c_str(), f.substr(a + 1).c_str(), cid);
                for (float v : npu.last_logits) fprintf(dump, ",%.5f", v);
                fprintf(dump, "\n");
            }
            total++;
        }
        if (dump) fclose(dump);
        int diag = 0, okng_ok = 0;
        auto is_ok = [&](int c) { const std::string &l = npu.classes()[c]; return l.size() > 3 && l.compare(l.size() - 3, 3, "_ok") == 0; };
        printf("== NPU accuracy on %s (%d crops)\nrows = true, cols = predicted\n", ev, total);
        for (int t = 0; t < nc; t++) {
            int row = 0;
            printf("  %d %-12s", t, npu.classes()[t].c_str());
            for (int p = 0; p < nc; p++) {
                int v = conf[size_t(t * nc + p)];
                printf(" %4d", v);
                row += v;
                if (t == p) diag += v;
                if (is_ok(t) == is_ok(p)) okng_ok += v;
            }
            printf("   recall %.3f\n", row ? double(conf[size_t(t * nc + t)]) / row : 0.0);
        }
        printf("== NPU top-1 accuracy %.4f, OK/NG two-class accuracy %.4f\n", total ? double(diag) / total : 0.0,
               total ? double(okng_ok) / total : 0.0);
    }

    std::vector<double> pre, run;
    for (int i = 0; i < repeats; i++) {
        double a = 0, b = 0;
        npu.classify(first, &a, &b);
        pre.push_back(a);
        run.push_back(b);
    }
    std::sort(pre.begin(), pre.end());
    std::sort(run.begin(), run.end());
    auto pct = [](const std::vector<double> &v, double q) { return v[std::min(v.size() - 1, size_t(q * v.size()))]; };
    printf("== latency over %d runs: input prep median %.2f ms; NPU run median %.2f ms, p95 %.2f ms, max %.2f ms\n",
           repeats, pct(pre, 0.5), pct(run, 0.5), pct(run, 0.95), run.back());

    int fail = 0;
    if (n == 0) fail++;
    if (ok_u8 < n * 0.95) { printf("FAIL: NPU disagrees with the PC uint8 model on %d of %d\n", n - ok_u8, n); fail++; }
    printf("%s\n", fail ? "FAILED" : "PASSED");
    return fail;
}
