#include "TestPatternSource.h"
#include "../core/Logger.h"

#include <cmath>
#include <algorithm>
#include <utility>

namespace lsalt {

namespace {
constexpr int kCellSize = 40;
constexpr uint8_t kCheckerA[3] = {34, 34, 38};
constexpr uint8_t kCheckerB[3] = {24, 24, 27};
constexpr uint8_t kCheckerA2[3] = {20, 40, 24}; // SceneCut's alternate palette
constexpr uint8_t kCheckerB2[3] = {14, 28, 16};

void SetPixel(std::vector<uint8_t>& buf, uint32_t w, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || y < 0 || x >= static_cast<int>(w)) return;
    const size_t idx = (static_cast<size_t>(y) * w + x) * 4;
    if (idx + 3 >= buf.size()) return;
    buf[idx + 0] = r;
    buf[idx + 1] = g;
    buf[idx + 2] = b;
    buf[idx + 3] = 255;
}

void DrawCheckerboard(std::vector<uint8_t>& buf, uint32_t w, uint32_t h, const uint8_t colorA[3], const uint8_t colorB[3]) {
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const bool a = ((x / kCellSize) + (y / kCellSize)) % 2 == 0;
            const uint8_t* c = a ? colorA : colorB;
            SetPixel(buf, w, x, y, c[0], c[1], c[2]);
        }
    }
}

void DrawVerticalStripe(std::vector<uint8_t>& buf, uint32_t w, uint32_t h, float x, int thickness,
                         uint8_t r, uint8_t g, uint8_t b) {
    const int xi = static_cast<int>(std::round(x));
    for (int dx = -thickness / 2; dx <= thickness / 2; ++dx) {
        for (int y = 0; y < static_cast<int>(h); ++y) {
            SetPixel(buf, w, xi + dx, y, r, g, b);
        }
    }
}

void DrawCircle(std::vector<uint8_t>& buf, uint32_t w, uint32_t h, float cx, float cy, float radius,
                 uint8_t r, uint8_t g, uint8_t b) {
    const int minX = std::max(0, static_cast<int>(cx - radius - 1));
    const int maxX = std::min(static_cast<int>(w) - 1, static_cast<int>(cx + radius + 1));
    const int minY = std::max(0, static_cast<int>(cy - radius - 1));
    const int maxY = std::min(static_cast<int>(h) - 1, static_cast<int>(cy + radius + 1));
    const float r2 = radius * radius;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float ddx = x - cx, ddy = y - cy;
            if (ddx * ddx + ddy * ddy <= r2) SetPixel(buf, w, x, y, r, g, b);
        }
    }
}

float Wrap(float v, float range) {
    float m = std::fmod(v, range);
    if (m < 0.0f) m += range;
    return m;
}

constexpr float kSlowPanSpeed = 40.0f;
constexpr float kFastPanSpeed = 300.0f;
constexpr float kSlowObjSpeed = 60.0f;
constexpr float kFastObjSpeed = 450.0f;
constexpr float kOcclusionSpeed = 150.0f;
constexpr float kObjectRadius = 24.0f;
} // namespace

const char* ToString(TestScenario s) {
    switch (s) {
        case TestScenario::Static: return "Static";
        case TestScenario::SlowPan: return "Slow Panning";
        case TestScenario::FastPan: return "Fast Panning";
        case TestScenario::SlowObject: return "Slow-Moving Object";
        case TestScenario::FastObject: return "Fast-Moving Object";
        case TestScenario::SceneCut: return "Scene Transition";
        case TestScenario::ComplexMotion: return "Complex Multi-Object Motion";
        case TestScenario::Occlusion: return "Occlusion / Disocclusion";
        default: return "Unknown";
    }
}

bool TestPatternSource::Initialize(ID3D11Device* device, uint32_t width, uint32_t height) {
    return Resize(device, width, height);
}

bool TestPatternSource::Resize(ID3D11Device* device, uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    if (FAILED(device->CreateTexture2D(&desc, nullptr, m_texA.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, m_texB.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(device->CreateShaderResourceView(m_texA.Get(), nullptr, m_srvA.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(device->CreateShaderResourceView(m_texB.Get(), nullptr, m_srvB.ReleaseAndGetAddressOf()))) return false;
    return true;
}

void TestPatternSource::Render(TestScenario scenario, double time, std::vector<uint8_t>& outRgba) const {
    outRgba.assign(static_cast<size_t>(m_width) * m_height * 4, 0);
    const float t = static_cast<float>(time);

    switch (scenario) {
        case TestScenario::Static: {
            DrawCheckerboard(outRgba, m_width, m_height, kCheckerA, kCheckerB);
            DrawCircle(outRgba, m_width, m_height, m_width * 0.5f, m_height * 0.5f, kObjectRadius, 255, 120, 40);
            break;
        }
        case TestScenario::SlowPan:
        case TestScenario::FastPan: {
            DrawCheckerboard(outRgba, m_width, m_height, kCheckerA, kCheckerB);
            const float speed = (scenario == TestScenario::SlowPan) ? kSlowPanSpeed : kFastPanSpeed;
            DrawVerticalStripe(outRgba, m_width, m_height, Wrap(speed * t, static_cast<float>(m_width)), 6, 60, 200, 255);
            break;
        }
        case TestScenario::SlowObject:
        case TestScenario::FastObject: {
            DrawCheckerboard(outRgba, m_width, m_height, kCheckerA, kCheckerB);
            const float speed = (scenario == TestScenario::SlowObject) ? kSlowObjSpeed : kFastObjSpeed;
            const float x = Wrap(speed * t, static_cast<float>(m_width));
            const float y = m_height * 0.5f + (scenario == TestScenario::FastObject ? 40.0f * std::sin(t * 3.14159f) : 0.0f);
            DrawCircle(outRgba, m_width, m_height, x, y, kObjectRadius, 255, 120, 40);
            break;
        }
        case TestScenario::SceneCut: {
            const int epoch = static_cast<int>(std::floor(time / 1.0));
            const bool alt = (epoch % 2) == 1;
            DrawCheckerboard(outRgba, m_width, m_height, alt ? kCheckerA2 : kCheckerA, alt ? kCheckerB2 : kCheckerB);
            DrawCircle(outRgba, m_width, m_height, m_width * 0.5f, m_height * 0.5f, kObjectRadius,
                       alt ? 60 : 255, alt ? 220 : 120, alt ? 90 : 40);
            break;
        }
        case TestScenario::ComplexMotion: {
            DrawCheckerboard(outRgba, m_width, m_height, kCheckerA, kCheckerB);
            DrawVerticalStripe(outRgba, m_width, m_height, Wrap(kSlowPanSpeed * t, static_cast<float>(m_width)), 4, 60, 200, 255);
            const float x1 = Wrap(80.0f * t, static_cast<float>(m_width));
            const float y1 = m_height * 0.3f;
            DrawCircle(outRgba, m_width, m_height, x1, y1, 18.0f, 255, 120, 40);
            const float x2 = static_cast<float>(m_width) - Wrap(150.0f * t, static_cast<float>(m_width));
            const float y2 = m_height * 0.7f;
            DrawCircle(outRgba, m_width, m_height, x2, y2, 22.0f, 90, 255, 140);
            const float x3 = m_width * 0.5f + 100.0f * std::sin(t * 1.5f);
            const float y3 = m_height * 0.5f + 60.0f * std::cos(t * 1.5f);
            DrawCircle(outRgba, m_width, m_height, x3, y3, 14.0f, 230, 60, 220);
            break;
        }
        case TestScenario::Occlusion: {
            DrawCheckerboard(outRgba, m_width, m_height, kCheckerA, kCheckerB);
            DrawCircle(outRgba, m_width, m_height, m_width * 0.5f, m_height * 0.5f, 70.0f, 90, 140, 200);
            const float x = Wrap(kOcclusionSpeed * t, static_cast<float>(m_width));
            DrawCircle(outRgba, m_width, m_height, x, m_height * 0.5f, 30.0f, 255, 200, 60);
            break;
        }
        default:
            break;
    }
}

void TestPatternSource::Upload(ID3D11DeviceContext* context, ID3D11Texture2D* tex, const std::vector<uint8_t>& rgba) {
    context->UpdateSubresource(tex, 0, nullptr, rgba.data(), m_width * 4, 0);
}

bool TestPatternSource::Tick(ID3D11DeviceContext* context, double realDtSeconds) {
    m_simTime += realDtSeconds;
    m_accumulator += realDtSeconds;
    const double interval = 1.0 / m_sourceFps;
    if (m_accumulator < interval) return false;
    m_accumulator = std::fmod(m_accumulator, interval);

    // B becomes the new A; render a fresh B at the current simulated time.
    std::swap(m_texA, m_texB);
    std::swap(m_srvA, m_srvB);
    m_lastFrameATime = m_lastFrameBTime;
    m_lastFrameBTime = m_simTime;

    std::vector<uint8_t> pixels;
    Render(m_scenario, m_simTime, pixels);
    Upload(context, m_texB.Get(), pixels);
    return true;
}

void TestPatternSource::RenderExplicit(ID3D11DeviceContext* context, double timeA, double timeB) {
    std::vector<uint8_t> pixels;
    Render(m_scenario, timeA, pixels);
    Upload(context, m_texA.Get(), pixels);
    Render(m_scenario, timeB, pixels);
    Upload(context, m_texB.Get(), pixels);
    m_lastFrameATime = timeA;
    m_lastFrameBTime = timeB;
}

TestSceneMarker TestPatternSource::MarkerAt(double time) const {
    const auto markers = MarkersAt(m_scenario, time);
    if (markers.empty()) return TestSceneMarker{};
    return markers.front();
}

std::vector<TestSceneMarker> TestPatternSource::MarkersAt(TestScenario scenario, double time) const {
    const float t = static_cast<float>(time);
    std::vector<TestSceneMarker> out;

    switch (scenario) {
        case TestScenario::Static:
            out.push_back({true, m_width * 0.5f, m_height * 0.5f});
            break;
        case TestScenario::SlowPan:
            out.push_back({true, Wrap(kSlowPanSpeed * t, static_cast<float>(m_width)), m_height * 0.5f});
            break;
        case TestScenario::FastPan:
            out.push_back({true, Wrap(kFastPanSpeed * t, static_cast<float>(m_width)), m_height * 0.5f});
            break;
        case TestScenario::SlowObject:
            out.push_back({true, Wrap(kSlowObjSpeed * t, static_cast<float>(m_width)), m_height * 0.5f});
            break;
        case TestScenario::FastObject:
            out.push_back({true, Wrap(kFastObjSpeed * t, static_cast<float>(m_width)),
                            m_height * 0.5f + 40.0f * std::sin(t * 3.14159f)});
            break;
        case TestScenario::Occlusion:
            out.push_back({true, Wrap(kOcclusionSpeed * t, static_cast<float>(m_width)), m_height * 0.5f});
            break;
        case TestScenario::ComplexMotion:
            out.push_back({true, Wrap(80.0f * t, static_cast<float>(m_width)), m_height * 0.3f});
            out.push_back({true, static_cast<float>(m_width) - Wrap(150.0f * t, static_cast<float>(m_width)), m_height * 0.7f});
            out.push_back({true, m_width * 0.5f + 100.0f * std::sin(t * 1.5f), m_height * 0.5f + 60.0f * std::cos(t * 1.5f)});
            break;
        case TestScenario::SceneCut:
        default:
            break; // no single tracked marker: content can change discontinuously by design
    }
    return out;
}

} // namespace lsalt
