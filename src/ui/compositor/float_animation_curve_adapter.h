// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_COMPOSITOR_FLOAT_ANIMATION_CURVE_ADAPTER_H_
#define UI_COMPOSITOR_FLOAT_ANIMATION_CURVE_ADAPTER_H_

#include "base/time/time.h"
#include "ui/base/animation/tween.h"

namespace ui {

class FloatAnimationCurveAdapter {
 public:
  FloatAnimationCurveAdapter(Tween::Type tween_type,
                             float initial_value,
                             float target_value,
                             base::TimeDelta duration);

  virtual ~FloatAnimationCurveAdapter() { }

 private:
  Tween::Type tween_type_;
  float initial_value_;
  float target_value_;
  base::TimeDelta duration_;
};

}  // namespace ui

#endif  // UI_COMPOSITOR_FLOAT_ANIMATION_CURVE_ADAPTER_H_
