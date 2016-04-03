// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/compositor/layer.h"

#include <algorithm>

#include "base/command_line.h"
#include "base/debug/trace_event.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "ui/base/animation/animation.h"
#include "ui/compositor/compositor_switches.h"
#include "ui/compositor/dip_util.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/display.h"
#include "ui/gfx/interpolated_transform.h"
#include "ui/gfx/point3_f.h"
#include "ui/gfx/point_conversions.h"
#include "ui/gfx/size_conversions.h"

namespace {

const ui::Layer* GetRoot(const ui::Layer* layer) {
  while (layer->parent())
    layer = layer->parent();
  return layer;
}

}  // namespace

namespace ui {

Layer::Layer()
    : type_(LAYER_TEXTURED),
      compositor_(NULL),
      parent_(NULL),
      visible_(true),
      force_render_surface_(false),
      fills_bounds_opaquely_(true),
      layer_updated_externally_(false),
      background_blur_radius_(0),
      layer_saturation_(0.0f),
      layer_brightness_(0.0f),
      layer_grayscale_(0.0f),
      layer_inverted_(false),
      layer_mask_(NULL),
      layer_mask_back_link_(NULL),
      zoom_(1),
      zoom_inset_(0),
      delegate_(NULL),
      scale_content_(true),
      device_scale_factor_(1.0f) {
}

Layer::Layer(LayerType type)
    : type_(type),
      compositor_(NULL),
      parent_(NULL),
      visible_(true),
      force_render_surface_(false),
      fills_bounds_opaquely_(true),
      layer_updated_externally_(false),
      background_blur_radius_(0),
      layer_saturation_(0.0f),
      layer_brightness_(0.0f),
      layer_grayscale_(0.0f),
      layer_inverted_(false),
      layer_mask_(NULL),
      layer_mask_back_link_(NULL),
      zoom_(1),
      zoom_inset_(0),
      delegate_(NULL),
      scale_content_(true),
      device_scale_factor_(1.0f) {
}

Layer::~Layer() {
  // Destroying the animator may cause observers to use the layer (and
  // indirectly the WebLayer). Destroy the animator first so that the WebLayer
  // is still around.
  if (animator_.get())
    animator_->SetDelegate(NULL);
  animator_ = NULL;
  if (compositor_)
    compositor_->SetRootLayer(NULL);
  if (parent_)
    parent_->Remove(this);
  if (layer_mask_)
    SetMaskLayer(NULL);
  if (layer_mask_back_link_)
    layer_mask_back_link_->SetMaskLayer(NULL);
  for (size_t i = 0; i < children_.size(); ++i)
    children_[i]->parent_ = NULL;
}

Compositor* Layer::GetCompositor() {
  return GetRoot(this)->compositor_;
}

float Layer::opacity() const {
  return 1.0f;
}

void Layer::SetCompositor(Compositor* compositor) {
  // This function must only be called to set the compositor on the root layer,
  // or to reset it.
  DCHECK(!compositor || !compositor_);
  DCHECK(!compositor || compositor->root_layer() == this);
  DCHECK(!parent_);
  compositor_ = compositor;
  if (compositor) {
    OnDeviceScaleFactorChanged(compositor->device_scale_factor());
    SendPendingThreadedAnimations();
  }
}

void Layer::Add(Layer* child) {
  DCHECK(!child->compositor_);
  if (child->parent_)
    child->parent_->Remove(child);
  child->parent_ = this;
  children_.push_back(child);
  child->OnDeviceScaleFactorChanged(device_scale_factor_);
  if (GetCompositor())
    child->SendPendingThreadedAnimations();
}

void Layer::Remove(Layer* child) {
  std::vector<Layer*>::iterator i =
      std::find(children_.begin(), children_.end(), child);
  DCHECK(i != children_.end());
  children_.erase(i);
  child->parent_ = NULL;
}

void Layer::StackAtTop(Layer* child) {
  if (children_.size() <= 1 || child == children_.back())
    return;  // Already in front.
  StackAbove(child, children_.back());
}

void Layer::StackAbove(Layer* child, Layer* other) {
  StackRelativeTo(child, other, true);
}

void Layer::StackAtBottom(Layer* child) {
  if (children_.size() <= 1 || child == children_.front())
    return;  // Already on bottom.
  StackBelow(child, children_.front());
}

void Layer::StackBelow(Layer* child, Layer* other) {
  StackRelativeTo(child, other, false);
}

bool Layer::Contains(const Layer* other) const {
  for (const Layer* parent = other; parent; parent = parent->parent()) {
    if (parent == this)
      return true;
  }
  return false;
}

void Layer::SetAnimator(LayerAnimator* animator) {
  if (animator)
    animator->SetDelegate(this);
  animator_ = animator;
}

LayerAnimator* Layer::GetAnimator() {
  if (!animator_.get())
    SetAnimator(LayerAnimator::CreateDefaultAnimator());
  return animator_.get();
}

void Layer::SetTransform(const gfx::Transform& transform) {
  GetAnimator()->SetTransform(transform);
}

gfx::Transform Layer::GetTargetTransform() const {
  if (animator_.get() && animator_->IsAnimatingProperty(
      LayerAnimationElement::TRANSFORM)) {
    return animator_->GetTargetTransform();
  }
  return transform();
}

void Layer::SetBounds(const gfx::Rect& bounds) {
  GetAnimator()->SetBounds(bounds);
}

gfx::Rect Layer::GetTargetBounds() const {
  if (animator_.get() && animator_->IsAnimatingProperty(
      LayerAnimationElement::BOUNDS)) {
    return animator_->GetTargetBounds();
  }
  return bounds_;
}

void Layer::SetMasksToBounds(bool masks_to_bounds) {
}

bool Layer::GetMasksToBounds() const {
  return false;
}

void Layer::SetOpacity(float opacity) {
  GetAnimator()->SetOpacity(opacity);
}

float Layer::GetCombinedOpacity() const {
  float opacity = this->opacity();
  Layer* current = this->parent_;
  while (current) {
    opacity *= current->opacity();
    current = current->parent_;
  }
  return opacity;
}

void Layer::SetBackgroundBlur(int blur_radius) {
  background_blur_radius_ = blur_radius;

  SetLayerBackgroundFilters();
}

void Layer::SetLayerSaturation(float saturation) {
  layer_saturation_ = saturation;
  SetLayerFilters();
}

void Layer::SetLayerBrightness(float brightness) {
  GetAnimator()->SetBrightness(brightness);
}

float Layer::GetTargetBrightness() const {
  if (animator_.get() && animator_->IsAnimatingProperty(
      LayerAnimationElement::BRIGHTNESS)) {
    return animator_->GetTargetBrightness();
  }
  return layer_brightness();
}

void Layer::SetLayerGrayscale(float grayscale) {
  GetAnimator()->SetGrayscale(grayscale);
}

float Layer::GetTargetGrayscale() const {
  if (animator_.get() && animator_->IsAnimatingProperty(
      LayerAnimationElement::GRAYSCALE)) {
    return animator_->GetTargetGrayscale();
  }
  return layer_grayscale();
}

void Layer::SetLayerInverted(bool inverted) {
  layer_inverted_ = inverted;
  SetLayerFilters();
}

void Layer::SetMaskLayer(Layer* layer_mask) {
  // The provided mask should not have a layer mask itself.
  DCHECK(!layer_mask ||
         (!layer_mask->layer_mask_layer() &&
          layer_mask->children().empty() &&
          !layer_mask->layer_mask_back_link_));
  DCHECK(!layer_mask_back_link_);
  if (layer_mask_ == layer_mask)
    return;
  // We need to de-reference the currently linked object so that no problem
  // arises if the mask layer gets deleted before this object.
  if (layer_mask_)
    layer_mask_->layer_mask_back_link_ = NULL;
  layer_mask_ = layer_mask;
  // We need to reference the linked object so that it can properly break the
  // link to us when it gets deleted.
  if (layer_mask) {
    layer_mask->layer_mask_back_link_ = this;
    layer_mask->OnDeviceScaleFactorChanged(device_scale_factor_);
  }
}

void Layer::SetBackgroundZoom(float zoom, int inset) {
  zoom_ = zoom;
  zoom_inset_ = inset;

  SetLayerBackgroundFilters();
}

void Layer::SetLayerFilters() {
}

void Layer::SetLayerBackgroundFilters() {
}

float Layer::GetTargetOpacity() const {
  if (animator_.get() && animator_->IsAnimatingProperty(
      LayerAnimationElement::OPACITY))
    return animator_->GetTargetOpacity();
  return opacity();
}

void Layer::SetVisible(bool visible) {
  GetAnimator()->SetVisibility(visible);
}

bool Layer::GetTargetVisibility() const {
  if (animator_.get() && animator_->IsAnimatingProperty(
      LayerAnimationElement::VISIBILITY))
    return animator_->GetTargetVisibility();
  return visible_;
}

bool Layer::IsDrawn() const {
  const Layer* layer = this;
  while (layer && layer->visible_)
    layer = layer->parent_;
  return layer == NULL;
}

bool Layer::ShouldDraw() const {
  return type_ != LAYER_NOT_DRAWN && GetCombinedOpacity() > 0.0f;
}

// static
void Layer::ConvertPointToLayer(const Layer* source,
                                const Layer* target,
                                gfx::Point* point) {
  if (source == target)
    return;

  const Layer* root_layer = GetRoot(source);
  CHECK_EQ(root_layer, GetRoot(target));

  if (source != root_layer)
    source->ConvertPointForAncestor(root_layer, point);
  if (target != root_layer)
    target->ConvertPointFromAncestor(root_layer, point);
}

bool Layer::GetTargetTransformRelativeTo(const Layer* ancestor,
                                         gfx::Transform* transform) const {
  const Layer* p = this;
  for (; p && p != ancestor; p = p->parent()) {
    gfx::Transform translation;
    translation.Translate(static_cast<float>(p->bounds().x()),
                          static_cast<float>(p->bounds().y()));
    // Use target transform so that result will be correct once animation is
    // finished.
    if (!p->GetTargetTransform().IsIdentity())
      transform->ConcatTransform(p->GetTargetTransform());
    transform->ConcatTransform(translation);
  }
  return p == ancestor;
}

// static
gfx::Transform Layer::ConvertTransformToCCTransform(
    const gfx::Transform& transform,
    float device_scale_factor) {
  gfx::Transform cc_transform;
  cc_transform.Scale(device_scale_factor, device_scale_factor);
  cc_transform.PreconcatTransform(transform);
  cc_transform.Scale(1.0f / device_scale_factor, 1.0f / device_scale_factor);
  return cc_transform;
}

void Layer::SetFillsBoundsOpaquely(bool fills_bounds_opaquely) {
  if (fills_bounds_opaquely_ == fills_bounds_opaquely)
    return;

  fills_bounds_opaquely_ = fills_bounds_opaquely;
}

void Layer::SetExternalTexture(Texture* texture) {
  // Hold a ref to the old |Texture| until we have updated all
  // compositor references to the texture id that it holds.
  scoped_refptr<ui::Texture> old_texture = texture_;

  DCHECK_EQ(type_, LAYER_TEXTURED);
  bool has_texture = !!texture;
  layer_updated_externally_ = has_texture;
  texture_ = texture;
}

void Layer::SetColor(SkColor color) {
  GetAnimator()->SetColor(color);
}

bool Layer::SchedulePaint(const gfx::Rect& invalid_rect) {
  if (type_ == LAYER_SOLID_COLOR || (!delegate_ && !texture_.get()))
    return false;

  damaged_region_.op(invalid_rect.x(),
                     invalid_rect.y(),
                     invalid_rect.right(),
                     invalid_rect.bottom(),
                     SkRegion::kUnion_Op);
  ScheduleDraw();
  return true;
}

void Layer::ScheduleDraw() {
  Compositor* compositor = GetCompositor();
  if (compositor)
    compositor->ScheduleDraw();
}

void Layer::SendDamagedRects() {
  for (size_t i = 0; i < children_.size(); ++i)
    children_[i]->SendDamagedRects();
}

void Layer::SuppressPaint() {
  if (!delegate_)
    return;
  delegate_ = NULL;
  for (size_t i = 0; i < children_.size(); ++i)
    children_[i]->SuppressPaint();
}

void Layer::OnDeviceScaleFactorChanged(float device_scale_factor) {
  if (device_scale_factor_ == device_scale_factor)
    return;
  if (animator_.get())
    animator_->StopAnimatingProperty(LayerAnimationElement::TRANSFORM);
  gfx::Transform transform = this->transform();
  device_scale_factor_ = device_scale_factor;
  SchedulePaint(gfx::Rect(bounds_.size()));
  if (delegate_)
    delegate_->OnDeviceScaleFactorChanged(device_scale_factor);
  for (size_t i = 0; i < children_.size(); ++i)
    children_[i]->OnDeviceScaleFactorChanged(device_scale_factor);
  if (layer_mask_)
    layer_mask_->OnDeviceScaleFactorChanged(device_scale_factor);
}

void Layer::SetForceRenderSurface(bool force) {
  if (force_render_surface_ == force)
    return;

  force_render_surface_ = force;
}

void Layer::StackRelativeTo(Layer* child, Layer* other, bool above) {
  DCHECK_NE(child, other);
  DCHECK_EQ(this, child->parent());
  DCHECK_EQ(this, other->parent());

  const size_t child_i =
      std::find(children_.begin(), children_.end(), child) - children_.begin();
  const size_t other_i =
      std::find(children_.begin(), children_.end(), other) - children_.begin();
  if ((above && child_i == other_i + 1) || (!above && child_i + 1 == other_i))
    return;

  const size_t dest_i =
      above ?
      (child_i < other_i ? other_i : other_i + 1) :
      (child_i < other_i ? other_i - 1 : other_i);
  children_.erase(children_.begin() + child_i);
  children_.insert(children_.begin() + dest_i, child);
}

bool Layer::ConvertPointForAncestor(const Layer* ancestor,
                                    gfx::Point* point) const {
  gfx::Transform transform;
  bool result = GetTargetTransformRelativeTo(ancestor, &transform);
  gfx::Point3F p(*point);
  transform.TransformPoint(p);
  *point = gfx::ToFlooredPoint(p.AsPointF());
  return result;
}

bool Layer::ConvertPointFromAncestor(const Layer* ancestor,
                                     gfx::Point* point) const {
  gfx::Transform transform;
  bool result = GetTargetTransformRelativeTo(ancestor, &transform);
  gfx::Point3F p(*point);
  transform.TransformPointReverse(p);
  *point = gfx::ToFlooredPoint(p.AsPointF());
  return result;
}

void Layer::SetBoundsImmediately(const gfx::Rect& bounds) {
  if (bounds == bounds_)
    return;

  base::Closure closure;
  if (delegate_)
    closure = delegate_->PrepareForLayerBoundsChange();
  bool was_move = bounds_.size() == bounds.size();
  bounds_ = bounds;

  if (!closure.is_null())
    closure.Run();

  if (was_move) {
    // Don't schedule a draw if we're invisible. We'll schedule one
    // automatically when we get visible.
    if (IsDrawn())
      ScheduleDraw();
  } else {
    // Always schedule a paint, even if we're invisible.
    SchedulePaint(gfx::Rect(bounds.size()));
  }
}

void Layer::SetTransformImmediately(const gfx::Transform& transform) {
}

void Layer::SetOpacityImmediately(float opacity) {
  ScheduleDraw();
}

void Layer::SetVisibilityImmediately(bool visible) {
  if (visible_ == visible)
    return;

  visible_ = visible;
}

void Layer::SetBrightnessImmediately(float brightness) {
  layer_brightness_ = brightness;
  SetLayerFilters();
}

void Layer::SetGrayscaleImmediately(float grayscale) {
  layer_grayscale_ = grayscale;
  SetLayerFilters();
}

void Layer::SetColorImmediately(SkColor color) {
  DCHECK_EQ(type_, LAYER_SOLID_COLOR);
  SetFillsBoundsOpaquely(SkColorGetA(color) == 0xFF);
}

void Layer::SetBoundsFromAnimation(const gfx::Rect& bounds) {
  SetBoundsImmediately(bounds);
}

void Layer::SetTransformFromAnimation(const gfx::Transform& transform) {
  SetTransformImmediately(transform);
}

void Layer::SetOpacityFromAnimation(float opacity) {
  SetOpacityImmediately(opacity);
}

void Layer::SetVisibilityFromAnimation(bool visibility) {
  SetVisibilityImmediately(visibility);
}

void Layer::SetBrightnessFromAnimation(float brightness) {
  SetBrightnessImmediately(brightness);
}

void Layer::SetGrayscaleFromAnimation(float grayscale) {
  SetGrayscaleImmediately(grayscale);
}

void Layer::SetColorFromAnimation(SkColor color) {
  SetColorImmediately(color);
}

void Layer::ScheduleDrawForAnimation() {
  ScheduleDraw();
}

const gfx::Rect& Layer::GetBoundsForAnimation() const {
  return bounds();
}

gfx::Transform Layer::GetTransformForAnimation() const {
  return transform();
}

float Layer::GetOpacityForAnimation() const {
  return opacity();
}

bool Layer::GetVisibilityForAnimation() const {
  return visible();
}

float Layer::GetBrightnessForAnimation() const {
  return layer_brightness();
}

float Layer::GetGrayscaleForAnimation() const {
  return layer_grayscale();
}

SkColor Layer::GetColorForAnimation() const {
  // WebColor is equivalent to SkColor, per WebColor.h.
  // The NULL check is here since this is invoked regardless of whether we have
  // been configured as LAYER_SOLID_COLOR.
  return SK_ColorBLACK;
}

float Layer::GetDeviceScaleFactor() const {
  return device_scale_factor_;
}

namespace{

struct HasAnimationId {
  HasAnimationId(int id): id_(id) {
  }

  bool operator()(int id) const {
    return id == id_;
  }

 private:
  int id_;
};

}

void Layer::RemoveThreadedAnimation(int animation_id) {
}

void Layer::SendPendingThreadedAnimations() {
  for (size_t i = 0; i < children_.size(); ++i)
    children_[i]->SendPendingThreadedAnimations();
}

gfx::Transform Layer::transform() const {
  gfx::Transform transform;
  transform.Scale(1.0f / device_scale_factor_, 1.0f / device_scale_factor_);
  //transform.Scale(device_scale_factor_, device_scale_factor_);
  return transform;
}

}  // namespace ui
