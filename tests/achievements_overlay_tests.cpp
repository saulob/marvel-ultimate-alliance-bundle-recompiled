// The F7 achievements overlay: window sizing, scroll input, the close gate, per-game theme and its wiring into the app.
// Headless: no window, no ImGui context.
// Run: mua_achievements_overlay_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "achievement_list.h"
#include "achievements_theme.h"
#include "overlay_rules.h"

using namespace mua;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

const std::filesystem::path kSrcDir = MUA_SRC_DIR;

std::string ReadSource(const char* name) {
  std::ifstream in(kSrcDir / name, std::ios::binary);
  std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  std::erase(text, '\r');
  return text;
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

std::string Between(const std::string& source, const char* from, const char* to) {
  const size_t start = source.find(from);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = source.find(to, start + 1);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool SameColor(const ImVec4& a, const ImVec4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }

void TestViewportSizing() {
  std::printf("window sizing\n");
  CHECK(kOverlayViewportFraction == 0.7f);
  CHECK(kOverlayViewportMargin == 16.0f);
  CHECK(kAchievementsMinWidth == 420.0f && kAchievementsMaxWidth == 550.0f);
  CHECK(kAchievementsMinHeight == 320.0f && kAchievementsMaxHeight == 450.0f);

  CHECK(FitWindowExtent(700.0f, 420.0f, 550.0f) == 490.0f);
  CHECK(FitWindowExtent(1280.0f, kAchievementsMinWidth, kAchievementsMaxWidth) == 550.0f);
  CHECK(FitWindowExtent(720.0f, kAchievementsMinHeight, kAchievementsMaxHeight) == 450.0f);
  CHECK(FitWindowExtent(3840.0f, kAchievementsMinWidth, kAchievementsMaxWidth) == 550.0f);
  CHECK(FitWindowExtent(2160.0f, kAchievementsMinHeight, kAchievementsMaxHeight) == 450.0f);
  CHECK(FitWindowExtent(500.0f, 420.0f, 550.0f) == 420.0f);
  CHECK(FitWindowExtent(400.0f, 420.0f, 550.0f) == 400.0f - 2.0f * kOverlayViewportMargin);
  CHECK(FitWindowExtent(10.0f, 420.0f, 550.0f) == 0.0f);
  CHECK(FitWindowExtent(0.0f, 420.0f, 550.0f) == 0.0f);
  for (float viewport : {320.0f, 480.0f, 720.0f, 1080.0f, 1440.0f, 4320.0f}) {
    const float extent = FitWindowExtent(viewport, kAchievementsMinHeight, kAchievementsMaxHeight);
    CHECK(extent <= viewport - 2.0f * kOverlayViewportMargin || extent == 0.0f);
    CHECK(extent <= kAchievementsMaxHeight);
  }
}

void TestFontScale() {
  std::printf("font scale\n");
  constexpr float kLauncherFontSize = 18.0f;
  CHECK(kAchievementsBodyFontScale == 1.00f);
  CHECK(kAchievementsHeadingFontScale == 1.05f);
  CHECK(kAchievementsFooterFontScale == 1.05f);
  CHECK(kAchievementsHeadingFontScale > kAchievementsBodyFontScale);
  CHECK(kLauncherFontSize * kAchievementsBodyFontScale == kLauncherFontSize);
  CHECK(kAchievementsMaxWidth == 550.0f && kAchievementsMaxHeight == 450.0f);

  const std::string overlay = ReadSource("achievements_overlay.cpp");
  CHECK(Has(overlay, "constexpr float kRowPadding = 4.0f;"));
  CHECK(Has(overlay, "constexpr float kFooterPadding = 6.0f;"));
  CHECK(Has(overlay, "base * kAchievementsFooterFontScale"));
  CHECK(Has(overlay, "ImGui::PushFont(fonts.strong, fonts.footer_size);\n    ImGui::TextColored(style.header_text, \"B  Back\");"));
  CHECK(Has(overlay, "ImGui::PushFont(fonts.strong, fonts.body_size);"));
  CHECK(!Has(overlay, "TextDisabled"));
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ImFont* LauncherSemiboldFont() {\n  return g_fonts.semibold;"));
  CHECK(Has(ReadSource("launcher.h"), "ImFont* LauncherSemiboldFont();"));
}

void TestCloseGate() {
  std::printf("close gate\n");
  CHECK(kReleaseTimeoutSeconds == 1.0);
  CHECK(InputReleased(false, 0.0, kReleaseTimeoutSeconds));
  CHECK(!InputReleased(true, 0.5, kReleaseTimeoutSeconds));
  CHECK(InputReleased(true, 1.0, kReleaseTimeoutSeconds));

  CloseGate idle;
  CHECK(!idle.requested());
  CHECK(!idle.Step(false, 10.0));

  CloseGate held;
  held.Request();
  CHECK(held.requested());
  CHECK(!held.Step(true, 10.0));
  CHECK(!held.Step(true, 10.9));
  CHECK(held.Step(false, 11.0));

  CloseGate released;
  released.Request();
  CHECK(released.Step(false, 5.0));

  CloseGate stuck;
  stuck.Request();
  CHECK(!stuck.Step(true, 20.0));
  CHECK(!stuck.Step(true, 20.99));
  CHECK(stuck.Step(true, 21.0));
}

void TestScrolling() {
  std::printf("scroll input\n");
  CHECK(kStickDeadzone == 0.25f);
  CHECK(kScrollPixelsPerSecond == 480.0f);
  CHECK(ScrollDirection(true, false, 0.0f) == -1.0f);
  CHECK(ScrollDirection(false, true, 0.0f) == 1.0f);
  CHECK(ScrollDirection(true, true, 0.0f) == 0.0f);
  CHECK(ScrollDirection(true, false, -1.0f) == -1.0f);
  CHECK(ScrollDirection(false, false, 0.0f) == 0.0f);
  CHECK(ScrollDirection(false, false, kStickDeadzone) == 0.0f);
  CHECK(ScrollDirection(false, false, -kStickDeadzone) == 0.0f);
  CHECK(ScrollDirection(false, false, 0.1f) == 0.0f);
  CHECK(ScrollDirection(false, false, 1.0f) == -1.0f);
  CHECK(ScrollDirection(false, false, -1.0f) == 1.0f);
  const float half = ScrollDirection(false, false, 0.625f);
  CHECK(half < 0.0f && half > -1.0f);
}

void TestSummary() {
  std::printf("achievement summary\n");
  std::vector<AchievementEntry> entries(3);
  const uint32_t scores[] = {10, 20, 50};
  const bool unlocked[] = {true, false, true};
  for (size_t i = 0; i < entries.size(); ++i) {
    entries[i].info.gamerscore = scores[i];
    entries[i].unlocked = unlocked[i];
  }
  const AchievementSummary summary = Summarize(entries);
  CHECK(summary.unlocked == 2);
  CHECK(summary.total == 3);
  CHECK(summary.earned_gamerscore == 60);
  CHECK(summary.total_gamerscore == 80);

  const AchievementSummary empty = Summarize(std::vector<AchievementEntry>{});
  CHECK(empty.total == 0 && empty.unlocked == 0 && empty.total_gamerscore == 0 && empty.earned_gamerscore == 0);
}

void TestGameThemes() {
  std::printf("per-game theme\n");
  for (GameId id : {GameId::kMua1, GameId::kMua1Gold, GameId::kMua2}) {
    const GamePalette palette = PaletteOf(id);
    const AchievementsTheme theme = AchievementsThemeFor(&Game(id));
    CHECK(SameColor(theme.style.progress_bar, ThemeColor(palette.primary)));
    CHECK(SameColor(theme.border, ThemeColor(palette.border)));
    CHECK(SameColor(theme.scrollbar_hovered, ThemeColor(palette.hover)));
    CHECK(!SameColor(theme.style.progress_bar, rex::ui::AchievementsStyle{}.progress_bar));
    CHECK(!SameColor(theme.style.row_unlocked_bg, rex::ui::AchievementsStyle{}.row_unlocked_bg));
    CHECK(theme.style.row_unlocked_bg.w == 0.55f);
  }

  const AchievementsTheme mua1 = AchievementsThemeFor(&Game(GameId::kMua1));
  const AchievementsTheme gold = AchievementsThemeFor(&Game(GameId::kMua1Gold));
  const AchievementsTheme mua2 = AchievementsThemeFor(&Game(GameId::kMua2));
  CHECK(!SameColor(mua1.style.progress_bar, gold.style.progress_bar));
  CHECK(!SameColor(mua1.style.progress_bar, mua2.style.progress_bar));
  CHECK(!SameColor(gold.style.progress_bar, mua2.style.progress_bar));
  CHECK(mua1.style.progress_bar.x > mua1.style.progress_bar.z);
  CHECK(mua2.style.progress_bar.z > mua2.style.progress_bar.x);

  const AchievementsTheme fallback = AchievementsThemeFor(nullptr);
  CHECK(SameColor(fallback.style.progress_bar, mua1.style.progress_bar));
  CHECK(SameColor(fallback.border, mua1.border));

  const rex::ui::AchievementsStyle defaults;
  CHECK(SameColor(mua1.style.badge_gamerscore, defaults.badge_gamerscore));
  CHECK(mua1.style.locked_desc.x > defaults.locked_desc.x);
  CHECK(SameColor(mua1.style.locked_desc, gold.style.locked_desc));
  CHECK(mua1.style.locked_desc.x < mua1.style.unlocked_desc.x);
  CHECK(SameColor(mua1.style.unlocked_desc, gold.style.unlocked_desc));
  CHECK(mua1.style.icon_size == defaults.icon_size);
  CHECK(mua1.style.window_padding.x == defaults.window_padding.x);

  const std::string theme_source = ReadSource("achievements_theme.h");
  CHECK(Has(theme_source, "PaletteOf("));
  for (const char* hex : {"D73A3A", "C89A2B", "3B6FB6"}) {
    CHECK(!Has(theme_source, hex));
  }
}

void TestOverlayFeatures() {
  std::printf("overlay layout and controls\n");
  const std::string overlay = ReadSource("achievements_overlay.cpp");
  CHECK(Has(overlay, "ImFont* body = LauncherRegularFont();"));
  CHECK(Has(overlay, "LauncherSemiboldFont() ? LauncherSemiboldFont() : body"));
  CHECK(Has(overlay, "body->LegacySize"));
  CHECK(Has(overlay, "base * kAchievementsBodyFontScale"));
  CHECK(Has(overlay, "base * kAchievementsHeadingFontScale"));
  CHECK(!Has(overlay, "PushFont(LauncherRegularFont(), 0.0f)"));
  CHECK(!Has(overlay, "PushFont(fonts.body, 0.0f)"));
  CHECK(Has(overlay, "ImGui::PushFont(fonts.strong, fonts.heading_size);\n  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding"));
  CHECK(Has(overlay, "ImGui::PushFont(fonts.body, fonts.body_size);"));
  CHECK(Has(overlay, "\"X##close_achievements\""));
  CHECK(Has(overlay, "if (DrawCloseButton(style, fonts, header_row_y)) {\n      RequestClose();"));
  CHECK(Has(overlay, "ImVec2(size, size)"));
  CHECK(Has(overlay, "\"B  Back\""));
  CHECK(Has(overlay, "const float footer_height = fonts.footer_size + 2.0f * style.item_spacing.y"));
  CHECK(Has(overlay, "ImVec2(0.0f, -footer_height)"));
  CHECK(Has(overlay, "style.window_padding.x - size"));
  CHECK(Has(overlay, "FitWindowExtent(viewport.x, kAchievementsMinWidth, kAchievementsMaxWidth)"));
  CHECK(Has(overlay, "FitWindowExtent(viewport.y, kAchievementsMinHeight, kAchievementsMaxHeight)"));
  CHECK(Has(overlay, "ImGuiCond_Always, ImVec2(0.5f, 0.5f)"));
  CHECK(Has(overlay, "%d / %d unlocked"));
  CHECK(Has(overlay, "%dG / %dG"));
  CHECK(Has(overlay, "Unlock progress is saved automatically"));
  CHECK(Has(overlay, "ImGuiKey_GamepadDpadUp"));
  CHECK(Has(overlay, "ImGuiKey_GamepadLStickUp"));
  CHECK(Has(overlay, "ImGuiKey_UpArrow"));
  CHECK(Has(overlay, "kScrollPixelsPerSecond * io.DeltaTime"));
  CHECK(Has(overlay, "AchievementsThemeFor(ActiveGame())"));
  CHECK(!Has(overlay, "style().achievements"));
  CHECK(!Has(overlay, "AddFontFromFileTTF"));
  CHECK(!Has(overlay, "\"Close\""));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ImFont* LauncherRegularFont() {\n  return g_fonts.regular;"));
  CHECK(Has(ReadSource("launcher.h"), "ImFont* LauncherRegularFont();"));
}

void TestModalInput() {
  std::printf("modal input\n");
  const std::string modal = ReadSource("modal_dialog.cpp");
  CHECK(Has(modal, "ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)"));
  CHECK(Has(modal, "e.virtual_key() != rex::ui::VirtualKey::kEscape"));
  CHECK(Has(modal, "open->RequestClose();"));
  CHECK(Has(modal, "close_.Step(pressed_.any(), ImGui::GetTime())"));
  CHECK(Has(modal, "SetCursorOverlayOpen(true)"));
  CHECK(Has(modal, "SetCursorOverlayOpen(false)"));
  CHECK(Has(modal, "gamepad_.Release(io);"));

  const std::string host = ReadSource("window_host.cpp");
  CHECK(Has(host, "void SetCursorOverlayOpen(bool open) {"));
  CHECK(Has(host, "g_cursor.SetOverlayOpen(open);"));
}

void TestOwnership() {
  std::printf("F7 ownership\n");
  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override"));
  const std::string create = Between(app, "CreateAchievementsOverlay() override", "bool GameInputActive()");
  CHECK(Has(create, "achievements_->RequestClose();"));
  CHECK(Has(create, "OpenAchievements();"));
  CHECK(Has(create, "return nullptr;"));
  CHECK(!Has(app, "rex/ui/overlay/achievements_overlay.h"));
  CHECK(!Has(app, "rex::ui::AchievementsOverlay"));
  CHECK(!Has(app, "std::make_unique<rex::ui::AchievementsOverlay"));

  const std::string open = Between(app, "void OpenAchievements()", "static bool IsLauncher()");
  CHECK(Has(open, "achievements_ ||"));
  CHECK(Has(open, "mua::ModalDialog::AnyOpen()"));
  CHECK(Has(open, "new mua::AchievementsOverlay("));

  const std::string input = Between(app, "bool GameInputActive()", "void OpenAchievements()");
  CHECK(Has(input, "if (mua::ModalDialog::AnyOpen()) {\n      return false;"));
  CHECK(Has(app, "input->SetActiveCallback([this] { return GameInputActive(); });"));
  CHECK(Has(app, "mua::ModalDialog::AttachEscapeKey(window());"));

  const std::string shutdown = Between(app, "void OnShutdown() override", "void OnClosing(");
  CHECK(Has(shutdown, "mua::ModalDialog::DetachEscapeKey();"));
  CHECK(Has(shutdown, "delete achievements_;"));
  CHECK(shutdown.find("delete achievements_;") < shutdown.find("DetachWindowHost();"));

  const std::string cmake = ReadSource("../CMakeLists.txt");
  CHECK(Has(cmake, "src/achievements_overlay.cpp"));
  CHECK(Has(cmake, "src/modal_dialog.cpp"));
}

}  // namespace

int main() {
  TestViewportSizing();
  TestFontScale();
  TestCloseGate();
  TestScrolling();
  TestSummary();
  TestGameThemes();
  TestOverlayFeatures();
  TestModalInput();
  TestOwnership();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
