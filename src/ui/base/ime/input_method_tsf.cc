// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/ime/input_method_tsf.h"

#include "ui/base/ime/text_input_client.h"
#include "ui/base/ime/win/tsf_bridge.h"
#include "ui/base/ime/win/tsf_event_router.h"

namespace ui {

class InputMethodTSF::TSFEventObserver : public TSFEventRouterObserver {
 public:
  TSFEventObserver() : is_candidate_popup_open_(false) {}

  // Returns true if we know for sure that a candidate window (or IME suggest,
  // etc.) is open.
  bool IsCandidatePopupOpen() const { return is_candidate_popup_open_; }

  // Overridden from TSFEventRouterObserver:
  virtual void OnCandidateWindowCountChanged(size_t window_count) OVERRIDE {
    is_candidate_popup_open_ = (window_count != 0);
  }

 private:
  // True if we know for sure that a candidate window is open.
  bool is_candidate_popup_open_;

  DISALLOW_COPY_AND_ASSIGN(TSFEventObserver);
};

InputMethodTSF::InputMethodTSF(internal::InputMethodDelegate* delegate,
                               HWND toplevel_window_handle)
    : InputMethodWin(delegate, toplevel_window_handle),
      tsf_event_observer_(new TSFEventObserver()),
      tsf_event_router_(new TSFEventRouter(tsf_event_observer_.get())) {
  // In non-Aura environment, appropriate callbacks to OnFocus() and OnBlur()
  // are not implemented yet. To work around this limitation, here we use
  // "always focused" model.
  // TODO(ime): Fix the caller of OnFocus() and OnBlur() so that appropriate
  // focus event will be passed.
  InputMethodWin::OnFocus();
}

InputMethodTSF::~InputMethodTSF() {}

void InputMethodTSF::OnFocus() {
  // Do not call baseclass' OnFocus() and discard the event being in
  // "always focused" model.  See the comment in the constructor.
  // TODO(ime): Implement OnFocus once the callers are fixed.

  tsf_event_router_->SetManager(
      ui::TSFBridge::GetInstance()->GetThreadManager());
}

void InputMethodTSF::OnBlur() {
  // Do not call baseclass' OnBlur() and discard the event being in
  // "always focused" model.  See the comment in the constructor.
  // TODO(ime): Implement OnFocus once the callers are fixed.

  tsf_event_router_->SetManager(NULL);
}

bool InputMethodTSF::OnUntranslatedIMEMessage(
    const base::NativeEvent& event, InputMethod::NativeEventResult* result) {
  LRESULT original_result = 0;
  BOOL handled = FALSE;
  // Even when TSF is enabled, following IMM32/Win32 messages must be handled.
  switch (event.message) {
    case WM_IME_REQUEST:
      // Some TSF-native TIPs (Text Input Processors) such as ATOK and Mozc
      // still rely on WM_IME_REQUEST message to implement reverse conversion.
      original_result = OnImeRequest(
          event.message, event.wParam, event.lParam, &handled);
      break;
    case WM_CHAR:
    case WM_SYSCHAR:
      // ui::InputMethod interface is responsible for handling Win32 character
      // messages. For instance, we will be here in the following cases.
      // - TIP is not activated. (e.g, the current language profile is English)
      // - TIP does not handle and WM_KEYDOWN and WM_KEYDOWN is translated into
      //   WM_CHAR by TranslateMessage API. (e.g, TIP is turned off)
      // - Another application sends WM_CHAR through SendMessage API.
      original_result = OnChar(
          event.hwnd, event.message, event.wParam, event.lParam, &handled);
      break;
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
      // See the comment in WM_CHAR/WM_SYSCHAR.
      original_result = OnDeadChar(
          event.message, event.wParam, event.lParam, &handled);
      break;
  }
  if (result)
    *result = original_result;
  return !!handled;
}

void InputMethodTSF::OnTextInputTypeChanged(const TextInputClient* client) {
  if (IsTextInputClientFocused(client) && IsWindowFocused(client)) {
    ui::TSFBridge::GetInstance()->CancelComposition();
    ui::TSFBridge::GetInstance()->OnTextInputTypeChanged(client);
  }
  InputMethodWin::OnTextInputTypeChanged(client);
}

void InputMethodTSF::OnCaretBoundsChanged(const TextInputClient* client) {
  if (IsTextInputClientFocused(client) && IsWindowFocused(client))
    ui::TSFBridge::GetInstance()->OnTextLayoutChanged();
}

void InputMethodTSF::CancelComposition(const TextInputClient* client) {
  if (IsTextInputClientFocused(client) && IsWindowFocused(client))
    ui::TSFBridge::GetInstance()->CancelComposition();
}

void InputMethodTSF::SetFocusedTextInputClient(TextInputClient* client) {
  if (IsWindowFocused(client)) {
    if (IsTextInputClientFocused(client)) {
      ui::TSFBridge::GetInstance()->SetFocusedClient(
          GetAttachedWindowHandle(client), client);
    } else {
      // SetFocusedTextInputClient(NULL) must be interpreted as
      // "Remove the attached client".
      ui::TSFBridge::GetInstance()->RemoveFocusedClient(
          ui::TSFBridge::GetInstance()->GetFocusedTextInputClient());
    }
  }
  InputMethodWin::SetFocusedTextInputClient(client);
}

bool InputMethodTSF::IsCandidatePopupOpen() const {
  return tsf_event_observer_->IsCandidatePopupOpen();
}

void InputMethodTSF::OnWillChangeFocusedClient(TextInputClient* focused_before,
                                               TextInputClient* focused) {
  if (IsWindowFocused(focused_before)) {
    ConfirmCompositionText();
    ui::TSFBridge::GetInstance()->RemoveFocusedClient(focused_before);
  }
}

void InputMethodTSF::OnDidChangeFocusedClient(TextInputClient* focused_before,
                                              TextInputClient* focused) {
  if (IsWindowFocused(focused) && IsTextInputClientFocused(focused)) {
    ui::TSFBridge::GetInstance()->SetFocusedClient(
        GetAttachedWindowHandle(focused), focused);

    // Force to update the input type since client's TextInputStateChanged()
    // function might not be called if text input types before the client loses
    // focus and after it acquires focus again are the same.
    OnTextInputTypeChanged(focused);

    // Force to update caret bounds, in case the client thinks that the caret
    // bounds has not changed.
    OnCaretBoundsChanged(focused);
  }
}

void InputMethodTSF::ConfirmCompositionText() {
  if (!IsTextInputTypeNone())
    ui::TSFBridge::GetInstance()->ConfirmComposition();
}

bool InputMethodTSF::IsWindowFocused(const TextInputClient* client) const {
  HWND attached_window_handle = GetAttachedWindowHandle(client);
  return attached_window_handle && GetFocus() == attached_window_handle;
}

}  // namespace ui
