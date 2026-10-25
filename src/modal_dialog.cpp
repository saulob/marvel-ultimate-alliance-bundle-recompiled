#include "modal_dialog.h"

#include <atomic>
#include <cstddef>

#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>

#include "window_host.h"

namespace mua {
namespace {

// Above the ImGui drawer (64), so Escape is consumed before the drawer or the game see it.
constexpr size_t kInputZOrder = 1000;
constexpr ImGuiConfigFlags kNavFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

std::atomic<ModalDialog*> g_open{nullptr};

class EscapeKey final : public rex::ui::WindowInputListener {
 public:
  explicit EscapeKey(rex::ui::Window* window) : window_(window) {}

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.target() != window_ || e.virtual_key() != rex::ui::VirtualKey::kEscape) {
      return;
    }
    if (ModalDialog* open = g_open.load(std::memory_order_acquire)) {
      e.set_handled(true);
      if (!e.prev_state()) {
        open->RequestClose();
      }
    }
  }

 private:
  rex::ui::Window* window_;
};

EscapeKey* g_escape = nullptr;
rex::ui::Window* g_escape_window = nullptr;

}  // namespace

bool ModalDialog::AnyOpen() {
  return g_open.load(std::memory_order_acquire) != nullptr;
}

void ModalDialog::AttachEscapeKey(rex::ui::Window* window) {
  DetachEscapeKey();
  g_escape = new EscapeKey(window);
  g_escape_window = window;
  window->AddInputListener(g_escape, kInputZOrder);
}

void ModalDialog::DetachEscapeKey() {
  if (g_escape) {
    g_escape_window->RemoveInputListener(g_escape);
    delete g_escape;
    g_escape = nullptr;
    g_escape_window = nullptr;
  }
}

ModalDialog::ModalDialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {
  g_open.store(this, std::memory_order_release);
  ::SetCursorOverlayOpen(true);
}

ModalDialog::~ModalDialog() {
  ModalDialog* self = this;
  g_open.compare_exchange_strong(self, nullptr, std::memory_order_acq_rel);
  ::SetCursorOverlayOpen(false);
}

void ModalDialog::OnDraw(ImGuiIO& io) {
  if (!started_) {
    started_ = true;
    saved_nav_flags_ = io.ConfigFlags & kNavFlags;
    saved_has_gamepad_ = io.BackendFlags & ImGuiBackendFlags_HasGamepad;
    io.ConfigFlags |= kNavFlags;
  }
  gamepad_.Update(io);
  TrackPressedKeys();
  if (!close_.requested() && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
    close_.Request();
  }
  if (close_.requested()) {
    if (close_.Step(pressed_.any(), ImGui::GetTime())) {
      Close();
    }
    return;
  }
  DrawContent(io);
}

void ModalDialog::OnClose() {
  ImGuiIO& io = GetIO();
  gamepad_.Release(io);
  io.ConfigFlags = (io.ConfigFlags & ~kNavFlags) | saved_nav_flags_;
  io.BackendFlags = (io.BackendFlags & ~ImGuiBackendFlags_HasGamepad) | saved_has_gamepad_;
}

// The keys and buttons that went down while the dialog was open, until they come up again.
void ModalDialog::TrackPressedKeys() {
  for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
    if (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY) {
      continue;
    }
    const size_t index = size_t(key - ImGuiKey_NamedKey_BEGIN);
    if (ImGui::IsKeyPressed(ImGuiKey(key), false)) {
      pressed_.set(index);
    } else if (!ImGui::IsKeyDown(ImGuiKey(key))) {
      pressed_.reset(index);
    }
  }
}

}  // namespace mua
