#ifdef DRIFT_DEV

#include "ui/DevPanels.hpp"

#include <drift/App.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>

#include "imgui.h"

#include <drift/World.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten/heap.h>
#endif

#include <algorithm>
#include <memory>
#include <vector>

namespace drift {

namespace {

constexpr int kHistory = 240;

struct PerfState {
    float frameMs[kHistory] = {};
    int head = 0;
    int filled = 0;
    bool sortByTime = true;
};

const char* phaseName(Phase p) {
    switch (p) {
    case Phase::Startup:     return "Startup";
    case Phase::PreUpdate:   return "PreUpdate";
    case Phase::Update:      return "Update";
    case Phase::PostUpdate:  return "PostUpdate";
    case Phase::Extract:     return "Extract";
    case Phase::Render:      return "Render";
    case Phase::RenderFlush: return "RenderFlush";
    }
    return "?";
}

void drawPerformance(App& app, PerfState& st) {
    auto* time = app.getResource<Time>();
    float ms = time ? time->delta * 1000.f : 0.f;
    st.frameMs[st.head] = ms;
    st.head = (st.head + 1) % kHistory;
    st.filled = std::min(st.filled + 1, kHistory);

    // Oldest to newest for the plot.
    float ordered[kHistory];
    float sum = 0.f, worst = 0.f;
    for (int i = 0; i < st.filled; ++i) {
        int idx = (st.head - st.filled + i + kHistory) % kHistory;
        ordered[i] = st.frameMs[idx];
        sum += ordered[i];
        worst = std::max(worst, ordered[i]);
    }
    float avg = st.filled ? sum / static_cast<float>(st.filled) : 0.f;

    ImGui::Text("Frame %.2f ms  avg %.2f  worst %.2f", ms, avg, worst);
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.0f FPS", avg > 0.f ? 1000.f / avg : 0.f);
    ImGui::PlotLines("##frametime", ordered, st.filled, 0, overlay, 0.f,
                     std::max(33.4f, worst * 1.1f), ImVec2(-1.f, 80.f));

    if (auto* r = app.getResource<RendererResource>()) {
        ImGui::Text("Backend %s  draw calls %d  sprites %d", r->backendName(), r->drawCalls(),
                    r->spriteCount());
    }
    ImGui::Text("Entities %zu", app.world().entitiesWith(app.world().transform2dId()).size());
#ifdef __EMSCRIPTEN__
    ImGui::SameLine();
    ImGui::Text("  memory %.1f MiB", static_cast<double>(emscripten_get_heap_size()) / (1024.0 * 1024.0));
#endif

    // Milliseconds per phase: the sum of its systems' last run.
    std::vector<SystemTiming> timings = app.systemTimings();
    uint64_t phaseUs[7] = {};
    for (const auto& t : timings) {
        int i = static_cast<int>(t.phase);
        if (i >= 0 && i < 7) phaseUs[i] += t.microseconds;
    }
    ImGui::Separator();
    for (Phase p : {Phase::PreUpdate, Phase::Update, Phase::PostUpdate, Phase::Extract, Phase::Render,
                    Phase::RenderFlush}) {
        ImGui::Text("%-11s %6.2f ms", phaseName(p), static_cast<double>(phaseUs[static_cast<int>(p)]) / 1000.0);
    }

    ImGui::Separator();
    ImGui::Checkbox("Sort by time", &st.sortByTime);
    if (st.sortByTime) {
        std::stable_sort(timings.begin(), timings.end(), [](const SystemTiming& a, const SystemTiming& b) {
            return a.microseconds > b.microseconds;
        });
    }
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("systems", 3, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("System", ImGuiTableColumnFlags_WidthStretch, 3.f);
        ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("us", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableHeadersRow();
        for (const auto& t : timings) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(t.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(phaseName(t.phase));
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(t.microseconds));
        }
        ImGui::EndTable();
    }
}

} // namespace

UIResource::DevPanel makePerformancePanel() {
    auto state = std::make_shared<PerfState>();
    return [state](App& app) { drawPerformance(app, *state); };
}

} // namespace drift

#endif // DRIFT_DEV
