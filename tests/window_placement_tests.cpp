// Launcher window placement tests: what is saved, what is restored, when it is saved, and off-screen recovery.
// Pure logic with synthetic window states; the native window calls need a real window.
// Run: mua_window_placement_tests

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "window_placement.h"

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

struct FakeSettings {
  std::map<std::string, std::string> values;

  int GetInt(const char* key, int def) const {
    const auto it = values.find(key);
    return it == values.end() ? def : std::stoi(it->second);
  }
  bool GetBool(const char* key, bool def) const {
    const auto it = values.find(key);
    return it == values.end() ? def : it->second == "true";
  }
};

const Rect kScreen{0, 0, 1920, 1040};
const std::vector<Rect> kAreas{kScreen};
const Rect kNormal{200, 100, 1480, 820};

WindowState Normal(Rect rect) {
  return {rect, false, false};
}

FakeSettings Stored(const WindowPlacement& placement) {
  FakeSettings settings;
  for (const auto& [key, value] : PlacementValues(placement)) {
    settings.values[key] = value;
  }
  return settings;
}

void TestValues() {
  std::printf("stored values\n");
  const FakeSettings settings = Stored({kNormal, true});
  CHECK(settings.values.size() == 5);
  CHECK(settings.values.at("mua_launcher_x") == "200");
  CHECK(settings.values.at("mua_launcher_y") == "100");
  CHECK(settings.values.at("mua_launcher_width") == "1280");
  CHECK(settings.values.at("mua_launcher_height") == "720");
  CHECK(settings.values.at("mua_launcher_maximized") == "true");
  const WindowPlacement read = ReadPlacement(settings);
  CHECK(read.normal == kNormal && read.maximized);
  CHECK(!PlacementIsSet(ReadPlacement(FakeSettings{})));

  const Rect left{-1800, -40, -520, 680};
  const WindowPlacement round_trip = ReadPlacement(Stored({left, false}));
  CHECK(round_trip.normal == left && !round_trip.maximized);

  FakeSettings corrupt;
  corrupt.values = {{"mua_launcher_x", "10"},
                    {"mua_launcher_y", "10"},
                    {"mua_launcher_width", "-5"},
                    {"mua_launcher_height", "0"}};
  CHECK(!PlacementIsSet(ReadPlacement(corrupt)));
}

bool Visible(const Rect& rect, bool maximized) {
  return PlacementIsVisible(WindowPlacement{rect, maximized}, kAreas);
}

void TestVisibility() {
  std::printf("off-screen recovery\n");
  CHECK(Visible(kNormal, false));
  const std::vector<Rect> two{{-1920, 0, 0, 1040}, kScreen};
  const WindowPlacement left_monitor{{-1700, 100, -420, 820}, false};
  CHECK(PlacementIsVisible(left_monitor, two));
  CHECK(!PlacementIsVisible(left_monitor, kAreas));
  const WindowPlacement straddling{{-600, 100, 680, 820}, false};
  CHECK(PlacementIsVisible(straddling, two));
  CHECK(PlacementIsVisible(straddling, kAreas));
  CHECK(!Visible({3000, 100, 4280, 820}, false));
  CHECK(!Visible({-1500, 100, -220, 820}, false));
  CHECK(!Visible({200, 1030, 1480, 1750}, false));
  CHECK(!Visible({0, 0, 0, 0}, true));
  CHECK(!Visible({200, 100, 300, 150}, false));
  CHECK(!PlacementIsVisible(WindowPlacement{kNormal, false}, std::span<const Rect>()));
}

void TestMoveAndResize() {
  std::printf("move and resize\n");
  PlacementTracker tracker;
  tracker.Observe(Normal(kNormal), false, 0.0);
  CHECK(!tracker.TakeSettled(10.0).has_value());

  const Rect moved{300, 150, 1580, 870};
  tracker.Observe(Normal(moved), false, 11.0);
  CHECK(!tracker.TakeSettled(11.2).has_value());
  const auto saved = tracker.TakeSettled(11.6);
  CHECK(saved && saved->normal == moved && !saved->maximized);
  CHECK(!tracker.TakeSettled(20.0).has_value());

  const Rect first{300, 150, 1000, 700};
  const Rect second{300, 150, 1100, 750};
  tracker.Observe(Normal(first), false, 21.0);
  tracker.Observe(Normal(second), false, 21.3);
  CHECK(!tracker.TakeSettled(21.6).has_value());
  const auto resized = tracker.TakeSettled(21.9);
  CHECK(resized && resized->normal == second);
}

void TestMaximize() {
  std::printf("maximize and restore\n");
  PlacementTracker tracker(WindowPlacement{kNormal, false});
  tracker.Observe(Normal(kNormal), false, 0.0);
  tracker.Observe(WindowState{kScreen, true, false}, false, 1.0);
  const auto maximized = tracker.TakeSettled(2.0);
  CHECK(maximized && maximized->maximized && maximized->normal == kNormal);

  tracker.Observe(Normal(kNormal), false, 3.0);
  const auto restored = tracker.TakeSettled(4.0);
  CHECK(restored && !restored->maximized && restored->normal == kNormal);
}

void TestMinimizeAndFullscreen() {
  std::printf("minimized and fullscreen\n");
  PlacementTracker tracker(WindowPlacement{kNormal, true});
  tracker.Observe(WindowState{{-32000, -32000, -31840, -31972}, true, true}, false, 1.0);
  CHECK(!tracker.TakeSettled(5.0).has_value() && !tracker.Flush().has_value());

  PlacementTracker normal(WindowPlacement{kNormal, false});
  normal.Observe(WindowState{{-32000, -32000, -31840, -31972}, false, true}, false, 1.0);
  CHECK(!normal.Flush().has_value());
  normal.Observe(Normal(kScreen), true, 2.0);
  CHECK(!normal.Flush().has_value());
}

void TestFirstObservation() {
  std::printf("first observation\n");
  PlacementTracker tracker;
  tracker.Observe(Normal(kNormal), false, 0.0);
  CHECK(!tracker.Flush().has_value());

  PlacementTracker maximized_first;
  maximized_first.Observe(WindowState{kScreen, true, false}, false, 0.0);
  CHECK(!maximized_first.Flush().has_value());
  maximized_first.Observe(Normal(kNormal), false, 1.0);
  const auto restored = maximized_first.Flush();
  CHECK(restored && restored->normal == kNormal && !restored->maximized);
}

void TestFlushOnClose() {
  std::printf("flush on close\n");
  PlacementTracker tracker(WindowPlacement{kNormal, false});
  const Rect closed{10, 10, 1290, 730};
  tracker.Observe(Normal(closed), false, 5.0);
  const auto flushed = tracker.Flush();
  CHECK(flushed && flushed->normal == closed);
  CHECK(!tracker.Flush().has_value());
}

void TestPreShowPlan() {
  std::printf("pre-show plan\n");
  CHECK(!PlanPreShow(WindowPlacement{}, kAreas).Needed());

  const PreShowPlan normal = PlanPreShow(WindowPlacement{kNormal, false}, kAreas);
  CHECK(normal.Needed() && normal.normal == kNormal && !normal.maximized);

  const PreShowPlan maximized = PlanPreShow(WindowPlacement{kNormal, true}, kAreas);
  CHECK(maximized.normal == kNormal && maximized.maximized);

  const Rect gone{3000, 100, 4280, 820};
  const PreShowPlan recovered = PlanPreShow(WindowPlacement{gone, true}, kAreas);
  CHECK(!recovered.normal.has_value() && recovered.maximized && recovered.Needed());
  CHECK(!PlanPreShow(WindowPlacement{gone, false}, kAreas).Needed());

  const std::vector<Rect> two{{-1920, 0, 0, 1040}, kScreen};
  const PreShowPlan left = PlanPreShow(WindowPlacement{{-1700, 100, -420, 820}, false}, two);
  CHECK(left.normal && left.normal->left == -1700);
}

void TestRestoreAfterMaximizedStart() {
  std::printf("restore after a maximized start\n");
  PlacementTracker tracker(WindowPlacement{kNormal, true});
  tracker.Observe(WindowState{kScreen, true, false}, false, 0.0);
  CHECK(!tracker.Flush().has_value());
  tracker.Observe(Normal(kNormal), false, 1.0);
  const auto restored = tracker.Flush();
  CHECK(restored && !restored->maximized && restored->normal == kNormal);
}

void TestLauncherFullscreenIsSessionOnly() {
  std::printf("launcher fullscreen\n");
  for (const bool maximized : {false, true}) {
    PlacementTracker tracker(WindowPlacement{kNormal, maximized});
    FullscreenGate gate;
    gate.Request(true);
    CHECK(!gate.Update(false, 0.0).observe);
    CHECK(!gate.Update(false, 0.5).observe);
    const auto entered = gate.Update(true, 0.6);
    CHECK(!entered.observe && !entered.restore && !gate.Pending());
    tracker.Observe(Normal(kScreen), true, 0.7);
    CHECK(!tracker.TakeSettled(5.0).has_value() && !tracker.Flush().has_value());
    CHECK(!gate.Update(true, 1.0).observe);

    gate.Request(false);
    const auto left = gate.Update(false, 2.0);
    CHECK(left.restore && !left.observe);
    CHECK(tracker.Current() == WindowPlacement({kNormal, maximized}));
    CHECK(gate.Update(false, 2.1).observe);
  }

  FullscreenGate stuck;
  stuck.Request(true);
  CHECK(!stuck.Update(false, 0.0).observe);
  CHECK(stuck.Update(false, FullscreenGate::kTimeoutSeconds + 0.1).observe && !stuck.Pending());

  PlacementTracker closed(WindowPlacement{kNormal, true});
  closed.Observe(Normal(kScreen), true, 1.0);
  CHECK(!closed.Flush().has_value() && closed.Current() == WindowPlacement({kNormal, true}));
}

void TestGameSettingsStayApart() {
  std::printf("game settings\n");
  for (const auto& [key, value] : PlacementValues({kNormal, true})) {
    CHECK(key.starts_with("mua_launcher_"));
    CHECK(key != "fullscreen" && key != "monitor" && key != "window_width" && key != "window_height");
  }

  PlacementTracker tracker(WindowPlacement{kNormal, false});
  tracker.Observe(Normal(kScreen), true, 1.0);
  tracker.Observe(Normal({-1920, 0, 0, 1040}), true, 2.0);
  CHECK(!tracker.Flush().has_value());

  const Rect game_size{0, 0, 1920, 1080};
  CHECK(PlacementIsSet(WindowPlacement{game_size, false}));
  const FakeSettings stored = Stored({kNormal, true});
  CHECK(ReadPlacement(stored).normal == kNormal && ReadPlacement(stored).maximized);
}

void TestLauncherFullscreenPreference() {
  std::printf("launcher fullscreen preference\n");
  const auto stored = [](const WindowPlacement& placement, bool fullscreen) {
    FakeSettings settings = Stored(placement);
    for (const auto& [key, value] : LauncherFullscreenValues(fullscreen)) {
      settings.values[key] = value;
    }
    return settings;
  };

  CHECK(!ReadLauncherFullscreen(FakeSettings{}));
  CHECK(LauncherFullscreenValues(true).begin()->first == "mua_launcher_fullscreen");

  // Normal and maximized, closed without F11: the next start is not fullscreen.
  CHECK(!ReadLauncherFullscreen(stored({kNormal, false}, false)));
  CHECK(!ReadLauncherFullscreen(stored({kNormal, true}, false)));

  // Closed in fullscreen: fullscreen again, with the underlying state intact.
  for (const bool maximized : {false, true}) {
    const FakeSettings closed = stored({kNormal, maximized}, true);
    LauncherFullscreenPreference preference(ReadLauncherFullscreen(closed));
    CHECK(preference.wanted());
    const WindowPlacement under = ReadPlacement(closed);
    CHECK(under == WindowPlacement({kNormal, maximized}));

    PlacementTracker tracker(under);
    tracker.Observe(Normal(kScreen), true, 1.0);
    CHECK(!tracker.Flush().has_value() && tracker.Current() == under);

    FullscreenGate gate;
    gate.Request(false);
    CHECK(gate.Update(false, 2.0).restore && tracker.Current() == under);
  }

  // Leaving fullscreen for the game keeps the preference; the player's F11 changes it.
  LauncherFullscreenPreference preference(true);
  FullscreenGate gate;
  gate.Request(false);
  CHECK(gate.Update(false, 1.0).restore && preference.wanted());
  preference.UserSet(false);
  CHECK(!preference.wanted());
  preference.UserSet(true);
  CHECK(preference.wanted());

  // Game settings never reach the launcher key, and the launcher key is not a game key.
  for (const auto& [key, value] : LauncherFullscreenValues(true)) {
    CHECK(key.starts_with("mua_launcher_"));
    CHECK(key != "fullscreen" && key != "monitor" && key != "window_width" && key != "window_height");
  }
  FakeSettings game;
  game.values = {{"fullscreen", "true"}, {"monitor", "2"}, {"window_width", "1920"}, {"window_height", "1080"}};
  CHECK(!ReadLauncherFullscreen(game) && !PlacementIsSet(ReadPlacement(game)));
  FakeSettings launcher = stored({kNormal, false}, true);
  launcher.values["fullscreen"] = "false";
  CHECK(ReadLauncherFullscreen(launcher) && ReadPlacement(launcher).normal == kNormal);
}

}  // namespace

int main() {
  TestLauncherFullscreenPreference();
  TestLauncherFullscreenIsSessionOnly();
  TestGameSettingsStayApart();
  TestPreShowPlan();
  TestRestoreAfterMaximizedStart();
  TestValues();
  TestVisibility();
  TestMoveAndResize();
  TestMaximize();
  TestMinimizeAndFullscreen();
  TestFirstObservation();
  TestFlushOnClose();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
