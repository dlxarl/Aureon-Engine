#include "HeadTrackingPanel.h"
#include "Theme.h"
#include "Widgets.h"

#include <cstdio>

namespace {
    struct ProviderOption {
        HeadTrackerKind kind;
        const char* label;
        const char* description;
    };

    const ProviderOption PROVIDERS[] = {
        {HeadTrackerKind::Off, "Off", "The view follows the mouse and keys only."},
        {HeadTrackerKind::Mock, "Mock (no hardware)", "A head that looks around by itself, for trying the camera without AirPods."},
        {HeadTrackerKind::AirPods, "AirPods", "Head tracking from AirPods (3rd generation), AirPods Pro or AirPods Max. Needs macOS 14 or later."},
    };

    ImU32 stateColor(HeadTrackingState state) {
        switch (state) {
            case HeadTrackingState::Tracking: return Theme::Accent;
            case HeadTrackingState::Waiting: return Theme::Warning;
            case HeadTrackingState::Error: return Theme::Error;
            case HeadTrackingState::Unavailable: return Theme::Error;
            default: return Theme::TextDim;
        }
    }

    bool beginProperties(const char* id) {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 126.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void valueText(const char* label, const char* text) {
        Widgets::propertyLabel(label);
        ImGui::TextUnformatted(text);
    }
}

void HeadTrackingPanel::draw(EditorContext& ctx, bool* open) {
    if (!ImGui::Begin("Head Tracking", open)) {
        ImGui::End();
        return;
    }

    drawProvider(ctx);
    drawDiagnostics(ctx);
    drawSettings(ctx);
    ImGui::End();
}

void HeadTrackingPanel::drawProvider(EditorContext& ctx) {
    HeadTrackingController& head = ctx.headTracking;

    if (!Widgets::section("Provider")) return;

    if (beginProperties("##provider")) {
        const char* current = "Custom";
        for (const ProviderOption& option : PROVIDERS) {
            if (option.kind == head.getKind()) current = option.label;
        }
        Widgets::propertyLabel("Source", "Where the head's direction comes from.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##source", current)) {
            for (const ProviderOption& option : PROVIDERS) {
                if (ImGui::Selectable(option.label, option.kind == head.getKind())) head.setKind(option.kind);
                Widgets::tooltip(option.description);
            }
            ImGui::EndCombo();
        }

        Widgets::propertyLabel("Straight ahead", "Makes the way the head points now the middle of the view. It happens by itself when a device starts sending.");
        ImGui::BeginDisabled(head.getTracker() == nullptr);
        if (ImGui::Button("Recenter")) head.recenter();
        ImGui::EndDisabled();
        ImGui::EndTable();
    }

    if (head.getKind() == HeadTrackerKind::Off) {
        Widgets::wrappedDim("Choose a source. The view turns with the head in the Game view while playing; "
                            "the player still walks and aims where the mouse says.");
    }
}

void HeadTrackingPanel::drawDiagnostics(EditorContext& ctx) {
    HeadTrackingController& head = ctx.headTracking;
    const HeadTrackingDiagnostics info = head.diagnostics();

    if (!Widgets::section("Diagnostics")) return;

    if (beginProperties("##diagnostics")) {
        Widgets::propertyLabel("Status");
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::color(stateColor(info.state)));
        ImGui::TextUnformatted(toString(info.state));
        ImGui::PopStyleColor();

        valueText("Provider", info.provider.c_str());
        valueText("Available", head.getTracker() == nullptr ? "-" : (info.available ? "Yes" : "No"));

        char text[96];
        std::snprintf(text, sizeof(text), "%.1f Hz", info.updateHz);
        valueText("Update rate", text);

        std::snprintf(text, sizeof(text), "%llu", info.samples);
        valueText("Samples", text);

        std::snprintf(text, sizeof(text), "w %.3f  x %.3f  y %.3f  z %.3f", info.orientation.w, info.orientation.x,
                      info.orientation.y, info.orientation.z);
        valueText("Quaternion", text);

        const glm::vec3 angles = head.getEulerDegrees();
        std::snprintf(text, sizeof(text), "yaw %.1f   pitch %.1f   roll %.1f", angles.x, angles.y, angles.z);
        valueText("View offset", text);
        ImGui::EndTable();
    }

    if (!info.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::color(stateColor(info.state)));
        ImGui::TextWrapped("%s", info.error.c_str());
        ImGui::PopStyleColor();
    }

    // The mock stands in for hardware, so it can also play at failing.
    if (auto* mock = dynamic_cast<MockHeadTracker*>(head.getTracker())) {
        static bool disconnected = false;
        if (ImGui::Checkbox("Simulate disconnect", &disconnected)) mock->simulateDisconnect(disconnected);
        ImGui::SameLine();
        if (ImGui::Button("Simulate error")) mock->simulateError("Simulated sensor failure");
    }
}

void HeadTrackingPanel::drawSettings(EditorContext& ctx) {
    HeadTrackingSettings& settings = ctx.headTracking.settings;

    if (!Widgets::section("View")) return;

    if (beginProperties("##view")) {
        Widgets::propertyLabel("Sensitivity", "1 follows the head exactly; more turns the view further than the head.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##sensitivity", &settings.sensitivity, 0.25f, 3.0f, "%.2f");

        Widgets::propertyLabel("Smoothing", "Seconds the view takes to catch up with the head. 0 follows every sample.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##smoothing", &settings.smoothing, 0.0f, 0.3f, "%.2f s");

        Widgets::propertyLabel("Axes", "Which head movements turn the view.");
        ImGui::Checkbox("Yaw", &settings.yaw);
        ImGui::SameLine();
        ImGui::Checkbox("Pitch", &settings.pitch);
        ImGui::SameLine();
        ImGui::Checkbox("Roll", &settings.roll);
        ImGui::EndTable();
    }
}
