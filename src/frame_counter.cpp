#include "frame_counter.h"

#include <cfloat>
#include <chrono>
#include <string>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/keybinds.h>

#include "settings.h"
#include "skip_intro_notice.h"

namespace mua {
namespace {

constexpr ImU32 kPanelColor = IM_COL32(7, 14, 10, 190);
constexpr ImU32 kTextColor = IM_COL32(255, 255, 255, 255);

SkipIntroNotice g_skip_intro_notice;

ImU32 BorderColor() {
  const uint32_t rgb = FrameCounterAccent(ActiveGame());
  return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
}

double NowSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

class FrameCounterOverlay final : public rex::ui::ImGuiDialog {
 public:
  explicit FrameCounterOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const GuestFrameStats stats = GetGuestFrameStats();
    if (!FrameCounterVisible(REXCVAR_GET(mua_show_frame_counter), stats)) {
      return;
    }
    const std::string text = FrameCounterText(stats);
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const ImVec2 text_size = font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str());
    const ImVec2 pad(size * 0.6f, size * 0.3f);
    const ImVec2 p1(io.DisplaySize.x - size, size + text_size.y + pad.y * 2);
    const ImVec2 p0(p1.x - text_size.x - pad.x * 2, size);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(p0, p1, kPanelColor, size * 0.5f);
    draw->AddRect(p0, p1, BorderColor(), size * 0.5f, 0, 1.0f);
    draw->AddText(font, size, ImVec2(p0.x + pad.x, p0.y + pad.y), kTextColor, text.c_str());
  }
};

class SkipIntroNoticeOverlay final : public rex::ui::ImGuiDialog {
 public:
  explicit SkipIntroNoticeOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const bool counter_shown = REXCVAR_GET(mua_show_frame_counter);
    const bool counter_visible = FrameCounterVisible(counter_shown, GetGuestFrameStats());
    if (!g_skip_intro_notice.Visible(NowSeconds(), SkipIntroNoticeReady(counter_shown, counter_visible))) {
      return;
    }
    const char* text = g_skip_intro_notice.text();
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const ImVec2 text_size = font->CalcTextSizeA(size, FLT_MAX, 0, text);
    const ImVec2 pad(size * 0.6f, size * 0.3f);
    const float top = counter_visible ? size * 3.0f : size;
    const ImVec2 p1(io.DisplaySize.x - size, top + text_size.y + pad.y * 2);
    const ImVec2 p0(p1.x - text_size.x - pad.x * 2, top);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(p0, p1, kPanelColor, size * 0.5f);
    draw->AddRect(p0, p1, BorderColor(), size * 0.5f, 0, 1.0f);
    draw->AddText(font, size, ImVec2(p0.x + pad.x, p0.y + pad.y), kTextColor, text);
  }
};

}  // namespace

void CreateSkipIntroNotice(rex::ui::ImGuiDrawer* drawer) {
  new SkipIntroNoticeOverlay(drawer);  // retained by the drawer for the session
}

void QueueStartupSkipIntroNotice(bool skip_intro) {
  g_skip_intro_notice.QueueStartup(SkipIntroNoticeApplies(ActiveGame(), skip_intro), kSkipIntroStartupNoticeSeconds);
}

void CreateFrameCounter(rex::ui::ImGuiDrawer* drawer) {
  new FrameCounterOverlay(drawer);  // retained by the drawer for the session
  rex::ui::RegisterBind("bind_mua_frame_counter", "F2", "Toggle frame counter", [] {
    rex::cvar::SetFlagByName("mua_show_frame_counter", REXCVAR_GET(mua_show_frame_counter) ? "false" : "true");
  });
}

}  // namespace mua
