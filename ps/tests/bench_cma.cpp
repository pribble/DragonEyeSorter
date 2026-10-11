// bench_cma.cpp — how fast can the CPU read a 1920x1080 BGR CMA display buffer vs normal memory, and encode JPEG?
// 10-11 result: read 5.2 ms/frame, 960x540 JPEG ~42 ms -> no on-board recorder, use a USB HDMI capture card.
// Build: see the g++ line in the commit that added it (needs -larmnn for CmaBuf); run with LD_LIBRARY_PATH=/opt/...
#include <time.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "flow_common_types.hpp"

static double now_ms()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main()
{
    const int W = 1920, H = 1080;
    CmaBuf cma(W * H * 3);
    if (!cma) { fprintf(stderr, "CMA alloc failed\n"); return 1; }
    cv::Mat disp(H, W, CV_8UC3, cma->virt_addr), cached(H, W, CV_8UC3), half, out;
    cv::randu(cached, 0, 255);
    double t = now_ms();
    for (int i = 0; i < 5; i++) cached.copyTo(disp);
    printf("write cached -> CMA      : %6.2f ms / frame\n", (now_ms() - t) / 5);
    t = now_ms();
    for (int i = 0; i < 5; i++) disp.copyTo(cached);
    printf("read  CMA -> cached copy : %6.2f ms / frame\n", (now_ms() - t) / 5);
    t = now_ms();
    for (int i = 0; i < 5; i++) cv::resize(disp, half, cv::Size(W / 2, H / 2), 0, 0, cv::INTER_NEAREST);
    printf("resize CMA -> 960x540 (nearest): %6.2f ms\n", (now_ms() - t) / 5);
    t = now_ms();
    for (int i = 0; i < 5; i++) cv::resize(cached, half, cv::Size(W / 2, H / 2), 0, 0, cv::INTER_AREA);
    printf("resize cached -> 960x540 (area): %6.2f ms\n", (now_ms() - t) / 5);
    t = now_ms();
    for (int i = 0; i < 5; i++) cv::resize(cached, out, cv::Size(1280, 720), 0, 0, cv::INTER_LINEAR);
    printf("resize cached -> 1280x720 (linear): %6.2f ms\n", (now_ms() - t) / 5);
    std::vector<uchar> jpg;
    std::vector<int> q = {cv::IMWRITE_JPEG_QUALITY, 85};
    t = now_ms();
    for (int i = 0; i < 5; i++) cv::imencode(".jpg", out, jpg, q);
    printf("jpeg 1280x720 q85        : %6.2f ms, %zu kB (random content = worst case)\n", (now_ms() - t) / 5,
           jpg.size() / 1024);
    t = now_ms();
    for (int i = 0; i < 5; i++) cv::imencode(".jpg", half, jpg, q);
    printf("jpeg 960x540 q85         : %6.2f ms, %zu kB\n", (now_ms() - t) / 5, jpg.size() / 1024);
    return 0;
}
