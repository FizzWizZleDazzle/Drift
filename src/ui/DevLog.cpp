#ifdef DRIFT_DEV

#include "ui/DevPanels.hpp"

#include <drift/Log.hpp>

#include "imgui.h"

#include <memory>

namespace drift {

namespace {

struct LogState {
    uint64_t clearedThrough = 0;   // lines up to this id are hidden
    bool errorsOnly = false;
};

void drawLog(LogState& st) {
    std::vector<LogLine> lines = recentProblems();
    if (ImGui::Button("Clear") && !lines.empty()) st.clearedThrough = lines.back().id;
    ImGui::SameLine();
    ImGui::Checkbox("Errors only", &st.errorsOnly);
    ImGui::Separator();
    if (ImGui::BeginChild("lines")) {
        for (const LogLine& l : lines) {
            if (l.id <= st.clearedThrough) continue;
            bool error = l.level == LogLevel::Error;
            if (st.errorsOnly && !error) continue;
            ImGui::PushStyleColor(ImGuiCol_Text, error ? ImVec4(1.f, 0.45f, 0.4f, 1.f) : ImVec4(1.f, 0.85f, 0.35f, 1.f));
            ImGui::TextWrapped("%s", l.text.c_str());
            ImGui::PopStyleColor();
        }
        // Follow new lines while scrolled to the bottom.
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.f);
    }
    ImGui::EndChild();
}

} // namespace

UIResource::DevPanel makeLogPanel() {
    auto state = std::make_shared<LogState>();
    return [state](App&) { drawLog(*state); };
}

} // namespace drift

#endif // DRIFT_DEV
