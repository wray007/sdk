// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/compositor/transform_animation_curve_adapter.h"

namespace ui {

TransformAnimationCurveAdapter::TransformAnimationCurveAdapter(
    Tween::Type tween_type,
    gfx::Transform initial_value,
    gfx::Transform target_value,
    base::TimeDelta duration)
    : tween_type_(tween_type),
      initial_value_(initial_value),
      target_value_(target_value),
      duration_(duration) {
  gfx::DecomposeTransform(&decomposed_initial_value_, initial_value_);
  gfx::DecomposeTransform(&decomposed_target_value_, target_value_);
}

TransformAnimationCurveAdapter::~TransformAnimationCurveAdapter() {
}

}  // namespace ui
