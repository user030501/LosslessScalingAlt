#include "UIManager.h"
#include "panels/Panels.h"
#include "../App.h"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

namespace lsalt {

namespace {
struct NavItem {
    const char* label;
    void (*draw)(AppState&);
};

const NavItem kNavItems[] = {
    {"Home", &DrawHomePanel},
    {"Scaling", &DrawScalingPanel},
    {"Frame Generation", &DrawFrameGenPanel},
    {"Game Profiles", &DrawProfilesPanel},
    {"Performance Monitor", &DrawMonitorPanel},
    {"GPU Settings", &DrawGpuSettingsPanel},
    {"Advanced Settings", &DrawAdvancedPanel},
    {"About", &DrawAboutPanel},
};
} // namespace

bool UIManager::Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ApplyDarkTheme();

    if (!ImGui_ImplWin32_Init(hwnd)) return false;
    if (!ImGui_ImplDX11_Init(device, context)) return false;

    m_initialized = true;
    return true;
}

void UIManager::Shutdown() {
    if (!m_initialized) return;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void UIManager::NewFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void UIManager::Draw(AppState& state) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                              ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("##root", nullptr, flags);
    ImGui::PopStyleVar(2);

    ImGui::BeginChild("##sidebar", ImVec2(220, 0), true);
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "  LSAlt");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(kNavItems)); ++i) {
        bool selected = (state.activePanel == i);
        if (ImGui::Selectable(kNavItems[i].label, selected, 0, ImVec2(0, 32))) {
            state.activePanel = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##content", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Indent(8);
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", kNavItems[state.activePanel].label);
    ImGui::Spacing();
    kNavItems[state.activePanel].draw(state);
    ImGui::Unindent(8);
    ImGui::EndChild();

    ImGui::End();
}

void UIManager::Render() {
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void UIManager::ApplyDarkTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(10, 8);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]      = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    colors[ImGuiCol_ChildBg]       = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    colors[ImGuiCol_FrameBg]       = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]= ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.26f, 0.30f, 1.00f);
    colors[ImGuiCol_Header]        = ImVec4(0.20f, 0.45f, 0.70f, 0.55f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.55f, 0.85f, 0.75f);
    colors[ImGuiCol_HeaderActive]  = ImVec4(0.25f, 0.55f, 0.85f, 1.00f);
    colors[ImGuiCol_Button]        = ImVec4(0.20f, 0.45f, 0.70f, 0.65f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.55f, 0.85f, 0.85f);
    colors[ImGuiCol_ButtonActive]  = ImVec4(0.25f, 0.55f, 0.85f, 1.00f);
    colors[ImGuiCol_CheckMark]     = ImVec4(0.35f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]    = ImVec4(0.35f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_Separator]     = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    colors[ImGuiCol_Border]        = ImVec4(0.20f, 0.20f, 0.23f, 0.80f);
}

} // namespace lsalt
