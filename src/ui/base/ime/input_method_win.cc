// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/ime/input_method_win.h"

#include "base/basictypes.h"
#include "ui/base/events/event.h"
#include "ui/base/events/event_constants.h"
#include "ui/base/events/event_utils.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/base/win/hwnd_util.h"

namespace ui {
namespace {

// Extra number of chars before and after selection (or composition) range which
// is returned to IME for improving conversion accuracy.
static const size_t kExtraNumberOfChars = 20;

}  // namespace

InputMethodWin::InputMethodWin(internal::InputMethodDelegate* delegate,
                               HWND toplevel_window_handle)
    : active_(false),
      toplevel_window_handle_(toplevel_window_handle),
      direction_(base::i18n::UNKNOWN_DIRECTION),
      pending_requested_direction_(base::i18n::UNKNOWN_DIRECTION) {
  SetDelegate(delegate);
}

void InputMethodWin::Init(bool focused) {
  // Gets the initial input locale and text direction information.
  OnInputLocaleChanged();

  InputMethodBase::Init(focused);
}

bool InputMethodWin::DispatchKeyEvent(
    const base::NativeEvent& native_key_event) {
  if (native_key_event.message == WM_CHAR) {
    BOOL handled;
    OnChar(native_key_event.hwnd, native_key_event.message,
           native_key_event.wParam, native_key_event.lParam, &handled);
    return !!handled;  // Don't send WM_CHAR for post event processing.
  }
  // Handles ctrl-shift key to change text direction and layout alignment.
  if (ui::IMM32Manager::IsRTLKeyboardLayoutInstalled() &&
      !IsTextInputTypeNone()) {
    // TODO: shouldn't need to generate a KeyEvent here.
    const ui::KeyEvent key(native_key_event,
                           native_key_event.message == WM_CHAR);
    ui::KeyboardCode code = key.key_code();
    if (key.type() == ui::ET_KEY_PRESSED) {
      if (code == ui::VKEY_SHIFT) {
        base::i18n::TextDirection dir;
        if (ui::IMM32Manager::IsCtrlShiftPressed(&dir))
          pending_requested_direction_ = dir;
      } else if (code != ui::VKEY_CONTROL) {
        pending_requested_direction_ = base::i18n::UNKNOWN_DIRECTION;
      }
    } else if (key.type() == ui::ET_KEY_RELEASED &&
               (code == ui::VKEY_SHIFT || code == ui::VKEY_CONTROL) &&
               pending_requested_direction_ != base::i18n::UNKNOWN_DIRECTION) {
      GetTextInputClient()->ChangeTextDirectionAndLayoutAlignment(
          pending_requested_direction_);
      pending_requested_direction_ = base::i18n::UNKNOWN_DIRECTION;
    }
  }

  return DispatchKeyEventPostIME(native_key_event);
}

bool InputMethodWin::DispatchFabricatedKeyEvent(const ui::KeyEvent& event) {
  // TODO(ananta)
  // Support IMEs and RTL layout in Windows 8 metro Ash. The code below won't
  // work with IMEs.
  // Bug: https://code.google.com/p/chromium/issues/detail?id=164964
  if (event.is_char()) {
    if (GetTextInputClient()) {
      GetTextInputClient()->InsertChar(event.key_code(),
                                       ui::GetModifiersFromKeyState());
      return true;
    }
  }
  return DispatchFabricatedKeyEventPostIME(event.type(),
                                           event.key_code(),
                                           event.flags());
}

void InputMethodWin::OnInputLocaleChanged() {
  active_ = imm32_manager_.SetInputLanguage();
  locale_ = imm32_manager_.GetInputLanguageName();
  direction_ = imm32_manager_.GetTextDirection();
  OnInputMethodChanged();
}

std::string InputMethodWin::GetInputLocale() {
  return locale_;
}

base::i18n::TextDirection InputMethodWin::GetInputTextDirection() {
  return direction_;
}

bool InputMethodWin::IsActive() {
  return active_;
}

LRESULT InputMethodWin::OnImeRequest(UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam,
                                     BOOL* handled) {
  *handled = FALSE;

  // Should not receive WM_IME_REQUEST message, if IME is disabled.
  const ui::TextInputType type = GetTextInputType();
  if (type == ui::TEXT_INPUT_TYPE_NONE ||
      type == ui::TEXT_INPUT_TYPE_PASSWORD) {
    return 0;
  }

  switch (wparam) {
    case IMR_RECONVERTSTRING:
      *handled = TRUE;
      return OnReconvertString(reinterpret_cast<RECONVERTSTRING*>(lparam));
    case IMR_DOCUMENTFEED:
      *handled = TRUE;
      return OnDocumentFeed(reinterpret_cast<RECONVERTSTRING*>(lparam));
    case IMR_QUERYCHARPOSITION:
      *handled = TRUE;
      return OnQueryCharPosition(reinterpret_cast<IMECHARPOSITION*>(lparam));
    default:
      return 0;
  }
}

LRESULT InputMethodWin::OnChar(HWND window_handle,
                               UINT message,
                               WPARAM wparam,
                               LPARAM lparam,
                               BOOL* handled) {
  *handled = TRUE;

  // We need to send character events to the focused text input client event if
  // its text input type is ui::TEXT_INPUT_TYPE_NONE.
  if (GetTextInputClient()) {
    GetTextInputClient()->InsertChar(static_cast<char16>(wparam),
                                     ui::GetModifiersFromKeyState());

    // If Windows sends a WM_CHAR, then any previously sent WM_DEADCHARs (which
    // are displayed as the composition text) should be cleared.
    GetTextInputClient()->ClearCompositionText();
  }

  // Explicitly show the system menu at a good location on [Alt]+[Space].
  // Note: Setting |handled| to FALSE for DefWindowProc triggering of the system
  //       menu causes undesirable titlebar artifacts in the classic theme.
  if (message == WM_SYSCHAR && wparam == VK_SPACE)
    ui::ShowSystemMenu(window_handle);

  return 0;
}

LRESULT InputMethodWin::OnDeadChar(UINT message,
                                   WPARAM wparam,
                                   LPARAM lparam,
                                   BOOL* handled) {
  *handled = TRUE;

  if (IsTextInputTypeNone())
    return 0;

  if (!GetTextInputClient())
    return 0;

  // Shows the dead character as a composition text, so that the user can know
  // what dead key was pressed.
  ui::CompositionText composition;
  composition.text.assign(1, static_cast<char16>(wparam));
  composition.selection = ui::Range(0, 1);
  composition.underlines.push_back(
      ui::CompositionUnderline(0, 1, SK_ColorBLACK, false));
  GetTextInputClient()->SetCompositionText(composition);
  return 0;
}

LRESULT InputMethodWin::OnDocumentFeed(RECONVERTSTRING* reconv) {
  ui::TextInputClient* client = GetTextInputClient();
  if (!client)
    return 0;

  ui::Range text_range;
  if (!client->GetTextRange(&text_range) || text_range.is_empty())
    return 0;

  bool result = false;
  ui::Range target_range;
  if (client->HasCompositionText())
    result = client->GetCompositionTextRange(&target_range);

  if (!result || target_range.is_empty()) {
    if (!client->GetSelectionRange(&target_range) ||
        !target_range.IsValid()) {
      return 0;
    }
  }

  if (!text_range.Contains(target_range))
    return 0;

  if (target_range.GetMin() - text_range.start() > kExtraNumberOfChars)
    text_range.set_start(target_range.GetMin() - kExtraNumberOfChars);

  if (text_range.end() - target_range.GetMax() > kExtraNumberOfChars)
    text_range.set_end(target_range.GetMax() + kExtraNumberOfChars);

  size_t len = text_range.length();
  size_t need_size = sizeof(RECONVERTSTRING) + len * sizeof(WCHAR);

  if (!reconv)
    return need_size;

  if (reconv->dwSize < need_size)
    return 0;

  string16 text;
  if (!GetTextInputClient()->GetTextFromRange(text_range, &text))
    return 0;
  DCHECK_EQ(text_range.length(), text.length());

  reconv->dwVersion = 0;
  reconv->dwStrLen = len;
  reconv->dwStrOffset = sizeof(RECONVERTSTRING);
  reconv->dwCompStrLen =
      client->HasCompositionText() ? target_range.length() : 0;
  reconv->dwCompStrOffset =
      (target_range.GetMin() - text_range.start()) * sizeof(WCHAR);
  reconv->dwTargetStrLen = target_range.length();
  reconv->dwTargetStrOffset = reconv->dwCompStrOffset;

  memcpy((char*)reconv + sizeof(RECONVERTSTRING),
         text.c_str(), len * sizeof(WCHAR));

  // According to Microsoft API document, IMR_RECONVERTSTRING and
  // IMR_DOCUMENTFEED should return reconv, but some applications return
  // need_size.
  return reinterpret_cast<LRESULT>(reconv);
}

LRESULT InputMethodWin::OnReconvertString(RECONVERTSTRING* reconv) {
  ui::TextInputClient* client = GetTextInputClient();
  if (!client)
    return 0;

  // If there is a composition string already, we don't allow reconversion.
  if (client->HasCompositionText())
    return 0;

  ui::Range text_range;
  if (!client->GetTextRange(&text_range) || text_range.is_empty())
    return 0;

  ui::Range selection_range;
  if (!client->GetSelectionRange(&selection_range) ||
      selection_range.is_empty()) {
    return 0;
  }

  DCHECK(text_range.Contains(selection_range));

  size_t len = selection_range.length();
  size_t need_size = sizeof(RECONVERTSTRING) + len * sizeof(WCHAR);

  if (!reconv)
    return need_size;

  if (reconv->dwSize < need_size)
    return 0;

  // TODO(penghuang): Return some extra context to help improve IME's
  // reconversion accuracy.
  string16 text;
  if (!GetTextInputClient()->GetTextFromRange(selection_range, &text))
    return 0;
  DCHECK_EQ(selection_range.length(), text.length());

  reconv->dwVersion = 0;
  reconv->dwStrLen = len;
  reconv->dwStrOffset = sizeof(RECONVERTSTRING);
  reconv->dwCompStrLen = len;
  reconv->dwCompStrOffset = 0;
  reconv->dwTargetStrLen = len;
  reconv->dwTargetStrOffset = 0;

  memcpy(reinterpret_cast<char*>(reconv) + sizeof(RECONVERTSTRING),
         text.c_str(), len * sizeof(WCHAR));

  // According to Microsoft API document, IMR_RECONVERTSTRING and
  // IMR_DOCUMENTFEED should return reconv, but some applications return
  // need_size.
  return reinterpret_cast<LRESULT>(reconv);
}

LRESULT InputMethodWin::OnQueryCharPosition(IMECHARPOSITION* char_positon) {
  if (!char_positon)
    return 0;

  if (char_positon->dwSize < sizeof(IMECHARPOSITION))
    return 0;

  ui::TextInputClient* client = GetTextInputClient();
  if (!client)
    return 0;

  gfx::Rect rect;
  if (client->HasCompositionText()) {
    if (!client->GetCompositionCharacterBounds(char_positon->dwCharPos,
                                               &rect)) {
      return 0;
    }
  } else {
    // If there is no composition and the first character is queried, returns
    // the caret bounds. This behavior is the same to that of RichEdit control.
    if (char_positon->dwCharPos != 0)
      return 0;
    rect = client->GetCaretBounds();
  }

  char_positon->pt.x = rect.x();
  char_positon->pt.y = rect.y();
  char_positon->cLineHeight = rect.height();
  return 1;  // returns non-zero value when succeeded.
}

HWND InputMethodWin::GetAttachedWindowHandle(
  const TextInputClient* text_input_client) const {
  // On Aura environment, we can assume that |toplevel_window_handle_| always
  // represents the valid top-level window handle because each top-level window
  // is responsible for lifecycle management of corresponding InputMethod
  // instance.
#if defined(USE_AURA)
  return toplevel_window_handle_;
#else
  // On Non-Aura environment, TextInputClient::GetAttachedWindow() returns
  // window handle to which each input method is bound.
  if (!text_input_client)
    return NULL;
  return text_input_client->GetAttachedWindow();
#endif
}

}  // namespace ui
