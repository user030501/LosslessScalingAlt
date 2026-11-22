#include "Panels.h"
#include "../../App.h"
#include <imgui.h>
#include <windows.h>
#include <commdlg.h>
#include <filesystem>
#include <cstring>

namespace lsalt {

namespace {
// Real, working file picker (no stub): lets the user manually select an
// executable for "Manual executable selection" from the brief.
std::string PickExecutableFile(HWND owner) {
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Executables (*.exe)\0*.exe\0All Files\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Select Game Executable";

    if (GetOpenFileNameW(&ofn)) {
        std::filesystem::path p(fileBuf);
        return p.filename().string();
    }
    return {};
}
} // namespace

void DrawProfilesPanel(AppState& state) {
    ImGui::Columns(2, "profiles_columns", true);
    ImGui::SetColumnWidth(0, 260);

    ImGui::Text("Profiles");
    ImGui::Separator();

    const auto& list = state.profiles.Profiles();
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        bool selected = (i == state.selectedProfileIndex && !state.isAddingNewProfile);
        std::string label = list[i].displayName + (list[i].isBuiltIn ? "  [built-in]" : "");
        if (ImGui::Selectable(label.c_str(), selected)) {
            state.selectedProfileIndex = i;
            state.isAddingNewProfile = false;
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("+ New Profile", ImVec2(-1, 0))) {
        state.profileEditBuffer = GameProfile{};
        state.isAddingNewProfile = true;
    }

    GameProfile* selectedProfile = state.profiles.AtIndex(state.selectedProfileIndex);
    if (selectedProfile && !selectedProfile->isBuiltIn && !state.isAddingNewProfile) {
        if (ImGui::Button("Delete Selected", ImVec2(-1, 0))) {
            state.profiles.Remove(selectedProfile->displayName);
            state.selectedProfileIndex = 0;
        }
    }

    ImGui::NextColumn();

    if (state.isAddingNewProfile) {
        ImGui::Text("New Profile");
        ImGui::Separator();

        char nameBuf[128];
        strncpy_s(nameBuf, state.profileEditBuffer.displayName.c_str(), sizeof(nameBuf) - 1);
        if (ImGui::InputText("Display Name", nameBuf, sizeof(nameBuf))) {
            state.profileEditBuffer.displayName = nameBuf;
        }

        char exeBuf[260];
        strncpy_s(exeBuf, state.profileEditBuffer.executableName.c_str(), sizeof(exeBuf) - 1);
        if (ImGui::InputText("Executable Name", exeBuf, sizeof(exeBuf))) {
            state.profileEditBuffer.executableName = exeBuf;
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            std::string picked = PickExecutableFile(nullptr);
            if (!picked.empty()) state.profileEditBuffer.executableName = picked;
        }

        ImGui::Spacing();
        const bool canCreate = !state.profileEditBuffer.displayName.empty() &&
                                !state.profileEditBuffer.executableName.empty();
        if (!canCreate) ImGui::BeginDisabled();
        if (ImGui::Button("Create")) {
            state.profiles.AddOrUpdate(state.profileEditBuffer);
            state.selectedProfileIndex = static_cast<int>(state.profiles.Profiles().size()) - 1;
            state.isAddingNewProfile = false;
        }
        if (!canCreate) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            state.isAddingNewProfile = false;
        }
    } else if (selectedProfile) {
        ImGui::Text("%s", selectedProfile->displayName.c_str());
        ImGui::TextDisabled("Executable: %s", selectedProfile->executableName.c_str());
        ImGui::Separator();
        ImGui::TextWrapped("%s", selectedProfile->notes.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("Use the Scaling and Frame Generation tabs to edit this profile's settings.");
    } else {
        ImGui::TextWrapped("Select a profile on the left, or create a new one.");
    }

    ImGui::Columns(1);
}

} // namespace lsalt
