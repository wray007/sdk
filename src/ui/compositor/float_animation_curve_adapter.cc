// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/compositor/float_animation_curve_adapter.h"

namespace ui {

FloatAnimationCurveAdapter::FloatAnimationCurveAdapter(
    Tween::Type tween_type,
    float initial_value,
    float target_value,
    base::TimeDelta duration)
    : tween_type_(tween_type),
      initial_value_(initial_value),
      target_value_(target_value),
      duration_(duration) {
}

}  // namespace ui