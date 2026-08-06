/**************************************************************************/
/*  mode7_tile_map_layer.cpp                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             REDOT ENGINE                               */
/*                        https://redotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2024-present Redot Engine contributors                   */
/*                                          (see REDOT_AUTHORS.md)        */
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "mode7_tile_map_layer.h"

#include "scene/main/viewport.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/world_2d.h"
#include "servers/rendering_server.h"

// Private visibility bit used to isolate the real (unwarped) tiles from the
// main game viewport.  The bake SubViewport's canvas_cull_mask is set to this
// single bit so it can capture this node's tiles in isolation, while a
// correctly-configured main Viewport excludes this bit and only sees the
// warped display child on layer 0.
static const uint32_t MODE7_BAKE_VISIBILITY_BIT = 31;

/*************************************************************************/
/* CONSTRUCTION / DESTRUCTION                                            */
/*************************************************************************/

Mode7TileMapLayer::Mode7TileMapLayer() {
}

Mode7TileMapLayer::~Mode7TileMapLayer() {
	_mode7_cleanup();
}

/*************************************************************************/
/* NOTIFICATIONS                                                         */
/*************************************************************************/

void Mode7TileMapLayer::_notification(int p_what) {
	TileMapLayer::_notification(p_what);

	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_mode7_setup_composition();

			// Debounced connection to the inherited `changed` signal so that
			// tile edits trigger a rebake at most once per process frame.
			if (!is_connected(SNAME("changed"), callable_mp(this, &Mode7TileMapLayer::_on_tilemap_changed))) {
				connect(SNAME("changed"), callable_mp(this, &Mode7TileMapLayer::_on_tilemap_changed));
			}

			// Set visibility layer so the real tiles only render into our bake
			// SubViewport (which has canvas_cull_mask = bit 31) and NOT into the
			// default main viewport.  If mode7_enabled is false, we restore the
			// default layer so plain tiles show normally with zero overhead.
			if (is_mode7_enabled()) {
				set_visibility_layer(1u << MODE7_BAKE_VISIBILITY_BIT);
				_mode7_perform_bake();
			} else {
				set_visibility_layer(0x1); // default layer 0
			}
		} break;

		case NOTIFICATION_EXIT_TREE: {
			if (is_connected(SNAME("changed"), callable_mp(this, &Mode7TileMapLayer::_on_tilemap_changed))) {
				disconnect(SNAME("changed"), callable_mp(this, &Mode7TileMapLayer::_on_tilemap_changed));
			}
			_mode7_cleanup();
		} break;
	}
}

/*************************************************************************/
/* INTERNAL COMPOSITION SETUP                                            */
/*************************************************************************/

void Mode7TileMapLayer::_mode7_setup_composition() {
	if (!_mode7_bake_viewport) {
		_mode7_bake_viewport = memnew(SubViewport);
		_mode7_bake_viewport->set_transparent_background(true);
		_mode7_bake_viewport->set_disable_input(true);
		add_child(_mode7_bake_viewport, false, INTERNAL_MODE_FRONT);

		// Restrict bake viewport to our private visibility bit only.
		_mode7_bake_viewport->set_canvas_cull_mask(0);
		_mode7_bake_viewport->set_canvas_cull_mask_bit(MODE7_BAKE_VISIBILITY_BIT, true);
	}

	if (!_mode7_display) {
		_mode7_display = memnew(Mode7Sprite2D);
		add_child(_mode7_display, false, INTERNAL_MODE_FRONT);
		// Hide the display child until we have a baked texture.
		_mode7_display->set_visible(false);

		// Forward current property values to the new display instance.
		_mode7_display->set_mode7_tiling(is_mode7_tiling());
		_mode7_display->set_mode7_global_rotation(get_mode7_global_rotation());
		_mode7_display->set_mode7_global_pivot(get_mode7_global_pivot());
		_mode7_display->set_mode7_top_horizon_mask_amount(get_mode7_top_horizon_mask_amount());
		_mode7_display->set_mode7_top_horizon_mask_tilt(get_mode7_top_horizon_mask_tilt());
		_mode7_display->set_mode7_bottom_horizon_mask_amount(get_mode7_bottom_horizon_mask_amount());
		_mode7_display->set_mode7_bottom_horizon_mask_tilt(get_mode7_bottom_horizon_mask_tilt());
		_mode7_display->set_mode7_follow_horizon_tilts(is_mode7_follow_horizon_tilts());
		_mode7_display->set_mode7_follow_horizon_tilt_offset(get_mode7_follow_horizon_tilt_offset());
		_mode7_display->set_mode7_region_follow_target(get_mode7_region_follow_target());
		_mode7_display->set_mode7_scanline_overrides(get_mode7_scanline_overrides());

		// Enable Mode 7 on the display if it was enabled on us.  We do this last
		// (after all other properties) so _mode7_rebuild_material() picks up
		// everything in one pass, mirroring Mode7Sprite2D's own pattern.
		if (is_mode7_enabled()) {
			_mode7_display->set_mode7_enabled(true);
		}
	}
}

void Mode7TileMapLayer::_mode7_cleanup() {
	if (_mode7_display) {
		remove_child(_mode7_display);
		memdelete(_mode7_display);
		_mode7_display = nullptr;
	}
	if (_mode7_bake_viewport) {
		remove_child(_mode7_bake_viewport);
		memdelete(_mode7_bake_viewport);
		_mode7_bake_viewport = nullptr;
	}
	_mode7_pending_bake = false;
	_mode7_baked_texture.unref();
}

/*************************************************************************/
/* BAKE HELPERS                                                          */
/*************************************************************************/

// Compute the encompassing rect in local space from get_used_rect(), expanded
// by bake_margin.  Returns {rect, valid}.  Invalid when no TileSet or empty used rect.
void Mode7TileMapLayer::_mode7_compute_encompassing(Rect2 &r_rect) const {
	Rect2i used = get_used_rect();
	if (used.size.x <= 0 || used.size.y <= 0) {
		r_rect = Rect2();
		return;
	}

	// Expand by margin for edge sampling headroom.
	Rect2i bake_rect = used;
	bake_rect.position -= Vector2i(mode7_bake_margin, mode7_bake_margin);
	bake_rect.size += Vector2i(mode7_bake_margin * 2, mode7_bake_margin * 2);

	// Compute encompassing rect in local (pixel) space.
	Vector2 tl = map_to_local(bake_rect.position);
	Vector2 br = map_to_local(bake_rect.position + bake_rect.size);
	r_rect = Rect2(tl, br - tl);
}

/*************************************************************************/
/* BAKE                                                                  */
/*************************************************************************/

void Mode7TileMapLayer::_mode7_perform_bake() {
	if (!_mode7_bake_viewport || !_mode7_display) {
		return;
	}

	if (get_tile_set().is_null()) {
		return;
	}

	Rect2 encompassing_rect;
	_mode7_compute_encompassing(encompassing_rect);
	if (encompassing_rect.size.x <= 0.5f || encompassing_rect.size.y <= 0.5f) {
		return;
	}

	// Scale for quality/perf tuning.
	Size2 bake_size = encompassing_rect.size * mode7_bake_resolution_scale;
	bake_size.x = MAX(1.0f, Math::round(bake_size.x));
	bake_size.y = MAX(1.0f, Math::round(bake_size.y));

	_mode7_bake_viewport->set_size(Size2i(int(bake_size.x), int(bake_size.y)));

	// Set the canvas_transform so the SubViewport's capture origin is at the
	// encompassing_rect's top-left in local space.  This cancels out the node's
	// own position/rotation/scale - the bake captures canonical LOCAL tile data,
	// and the display child (as a normal scene-tree child) inherits transforms
	// for free when the parent moves.
	Transform2D ct;
	ct.set_origin(-encompassing_rect.position);

	RS::get_singleton()->viewport_set_canvas_transform(
			_mode7_bake_viewport->get_viewport_rid(),
			get_viewport()->find_world_2d()->get_canvas(),
			ct);

	if (mode7_bake_mode == BakeMode::BAKE_MODE_EVERY_FRAME) {
		// Live mode: feed the SubViewport's own texture directly to the display.
		// No snapshotting needed - continuous updates every frame.
		_mode7_bake_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);

		Ref<ViewportTexture> vp_tex = _mode7_bake_viewport->get_texture();
		if (vp_tex.is_valid()) {
			_mode7_display->set_texture(vp_tex);
		}

		// Position the display so it overlays the real tiles pixel-for-pixel.
		_mode7_display->set_centered(false);
		_mode7_display->set_offset(encompassing_rect.position);

		// The SubViewportTexture is bake_size pixels.  Set sprite scale so it
		// fills encompassing_rect exactly (accounting for resolution scale).
		// When bake_resolution_scale == 1.0, texture size == rect size -> scale=1.
		// At other scales, the texture is proportionally smaller/larger.
		int tex_w = MAX(vp_tex->get_width(), 1);
		int tex_h = MAX(vp_tex->get_height(), 1);
		_mode7_display->set_scale(Size2(
				encompassing_rect.size.x / real_t(tex_w),
				encompassing_rect.size.y / real_t(tex_h)
			));

		if (is_mode7_enabled()) {
			_mode7_display->set_visible(true);
		}
		return;
	}

	// Snapshot mode: bake once, capture ImageTexture.
	_mode7_bake_viewport->set_update_mode(SubViewport::UPDATE_ONCE);

	// One-shot connection to frame_pre_draw - fires after the viewport has
	// rendered its single frame but before the next draw cycle.
	if (RS::get_singleton()->is_connected(
				SNAME("frame_pre_draw"),
				callable_mp(this, &Mode7TileMapLayer::_mode7_on_bake_frame_drawn))) {
		RS::get_singleton()->disconnect(
				SNAME("frame_pre_draw"),
				callable_mp(this, &Mode7TileMapLayer::_mode7_on_bake_frame_drawn));
	}

	RS::get_singleton()->connect(
			SNAME("frame_pre_draw"),
			callable_mp(this, &Mode7TileMapLayer::_mode7_on_bake_frame_drawn),
			Object::CONNECT_ONE_SHOT);
}

void Mode7TileMapLayer::_mode7_on_bake_frame_drawn() {
	if (!_mode7_bake_viewport || !_mode7_display) {
		return;
	}

	Ref<Image> image = _mode7_bake_viewport->get_texture()->get_image();
	if (!image.is_valid()) {
		return;
	}

	Size2i img_size(image->get_width(), image->get_height());

	if (_mode7_baked_texture.is_null() ||
			_mode7_baked_texture->get_width() != img_size.x ||
			_mode7_baked_texture->get_height() != img_size.y) {
		_mode7_baked_texture = ImageTexture::create_from_image(image);
	} else {
		_mode7_baked_texture->update(image);
	}

	// Push baked texture into the display child.
	_mode7_display->set_texture(_mode7_baked_texture);

	// Position and size the display so it overlays the real tiles pixel-perfectly.
	Rect2 encompassing_rect;
	_mode7_compute_encompassing(encompassing_rect);
	if (encompassing_rect.size.x <= 0.5f || encompassing_rect.size.y <= 0.5f) {
		return;
	}

	// Offset: the baked image starts at local-space (0,0) after the canvas_transform
	// translation, so we must draw it back out at encompassing_rect.position.
	_mode7_display->set_centered(false);
	_mode7_display->set_offset(encompassing_rect.position);

	// Scale: match encompassing_rect size to baked texture dimensions.
	int tex_w = MAX(_mode7_baked_texture->get_width(), 1);
	int tex_h = MAX(_mode7_baked_texture->get_height(), 1);
	_mode7_display->set_scale(Size2(
			encompassing_rect.size.x / real_t(tex_w),
			encompassing_rect.size.y / real_t(tex_h)
		));

	if (is_mode7_enabled()) {
		_mode7_display->set_visible(true);
	}
}

void Mode7TileMapLayer::_mode7_trigger_rebake() {
	if (!is_mode7_enabled() || mode7_bake_mode == BakeMode::BAKE_MODE_EVERY_FRAME) {
		return; // In every-frame mode, the viewport updates continuously.
	}

	// Debounce: at most one rebake queued per frame via call_deferred.
	if (_mode7_pending_bake) {
		return;
	}
	_mode7_pending_bake = true;
	callable_mp(this, &Mode7TileMapLayer::_mode7_deferred_rebake).call_deferred();
}

void Mode7TileMapLayer::_mode7_deferred_rebake() {
	_mode7_pending_bake = false;
	if (is_mode7_enabled()) {
		_mode7_perform_bake();
	}
}

/*************************************************************************/
/* TILE DATA CHANGE HANDLER                                              */
/*************************************************************************/

void Mode7TileMapLayer::_on_tilemap_changed() {
	if (mode7_bake_mode != BakeMode::BAKE_MODE_ON_CHANGE) {
		return; // Only auto-rebake in ON_CHANGE mode.
	}
	_mode7_trigger_rebake();
}

/*************************************************************************/
/* PUBLIC SCRIPT-CALLABLE BAKE                                           */
/*************************************************************************/

void Mode7TileMapLayer::bake_mode7_texture() {
	if (!is_mode7_enabled()) {
		return;
	}
	_mode7_perform_bake();
}

/*************************************************************************/
/* FORWARDING SETTERS / GETTERS                                          */
/*************************************************************************/

void Mode7TileMapLayer::set_mode7_enabled(bool p_enabled) {
	if (_mode7_display) {
		_mode7_display->set_mode7_enabled(p_enabled);
	}

	// Gate the visibility layer switching and bake lifecycle on mode7_enabled.
	if (is_inside_tree()) {
		if (p_enabled) {
			set_visibility_layer(1u << MODE7_BAKE_VISIBILITY_BIT);
			_mode7_perform_bake();
		} else {
			set_visibility_layer(0x1); // Restore default layer 0.
			if (_mode7_display) {
				_mode7_display->set_visible(false);
			}
			if (_mode7_bake_viewport) {
				_mode7_bake_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
			}
		}
	}

	notify_property_list_changed();
}

bool Mode7TileMapLayer::is_mode7_enabled() const {
	if (_mode7_display) {
		return _mode7_display->is_mode7_enabled();
	}
	return false;
}

void Mode7TileMapLayer::set_mode7_scanline_overrides(const TypedArray<Mode7ScanlineOverride> &p_overrides) {
	if (_mode7_display) {
		_mode7_display->set_mode7_scanline_overrides(p_overrides);
	}
}

TypedArray<Mode7ScanlineOverride> Mode7TileMapLayer::get_mode7_scanline_overrides() const {
	if (_mode7_display) {
		return _mode7_display->get_mode7_scanline_overrides();
	}
	return TypedArray<Mode7ScanlineOverride>();
}

void Mode7TileMapLayer::set_mode7_tiling(bool p_tiling) {
	if (_mode7_display) {
		_mode7_display->set_mode7_tiling(p_tiling);
	}
}

bool Mode7TileMapLayer::is_mode7_tiling() const {
	if (_mode7_display) {
		return _mode7_display->is_mode7_tiling();
	}
	return false;
}

void Mode7TileMapLayer::set_mode7_global_rotation(real_t p_radians) {
	if (_mode7_display) {
		_mode7_display->set_mode7_global_rotation(p_radians);
	}
}

real_t Mode7TileMapLayer::get_mode7_global_rotation() const {
	if (_mode7_display) {
		return _mode7_display->get_mode7_global_rotation();
	}
	return 0.0f;
}

void Mode7TileMapLayer::set_mode7_global_pivot(const Vector2 &p_pivot) {
	if (_mode7_display) {
		_mode7_display->set_mode7_global_pivot(p_pivot);
	}
}

Vector2 Mode7TileMapLayer::get_mode7_global_pivot() const {
	if (_mode7_display) {
		return _mode7_display->get_mode7_global_pivot();
	}
	return Vector2(0.5f, 0.5f);
}

void Mode7TileMapLayer::set_mode7_top_horizon_mask_amount(real_t p_amount) {
	if (_mode7_display) {
		_mode7_display->set_mode7_top_horizon_mask_amount(p_amount);
	}
}

real_t Mode7TileMapLayer::get_mode7_top_horizon_mask_amount() const {
	if (_mode7_display) {
		return _mode7_display->get_mode7_top_horizon_mask_amount();
	}
	return 0.0f;
}

void Mode7TileMapLayer::set_mode7_top_horizon_mask_tilt(real_t p_radians) {
	if (_mode7_display) {
		_mode7_display->set_mode7_top_horizon_mask_tilt(p_radians);
	}
}

real_t Mode7TileMapLayer::get_mode7_top_horizon_mask_tilt() const {
	if (_mode7_display) {
		return _mode7_display->get_mode7_top_horizon_mask_tilt();
	}
	return 0.0f;
}

void Mode7TileMapLayer::set_mode7_bottom_horizon_mask_amount(real_t p_amount) {
	if (_mode7_display) {
		_mode7_display->set_mode7_bottom_horizon_mask_amount(p_amount);
	}
}

real_t Mode7TileMapLayer::get_mode7_bottom_horizon_mask_amount() const {
	if (_mode7_display) {
		return _mode7_display->get_mode7_bottom_horizon_mask_amount();
	}
	return 0.0f;
}

void Mode7TileMapLayer::set_mode7_bottom_horizon_mask_tilt(real_t p_radians) {
	if (_mode7_display) {
		_mode7_display->set_mode7_bottom_horizon_mask_tilt(p_radians);
	}
}

real_t Mode7TileMapLayer::get_mode7_bottom_horizon_mask_tilt() const {
	if (_mode7_display) {
		return _mode7_display->get_mode7_bottom_horizon_mask_tilt();
	}
	return 0.0f;
}

void Mode7TileMapLayer::set_mode7_follow_horizon_tilts(bool p_enabled) {
	if (_mode7_display) {
		_mode7_display->set_mode7_follow_horizon_tilts(p_enabled);
	}
}

bool Mode7TileMapLayer::is_mode7_follow_horizon_tilts() const {
	if (_mode7_display) {
		return _mode7_display->is_mode7_follow_horizon_tilts();
	}
	return false;
}

void Mode7TileMapLayer::set_mode7_follow_horizon_tilt_offset(real_t p_radians) {
	if (_mode7_display) {
		_mode7_display->set_mode7_follow_horizon_tilt_offset(p_radians);
	}
}

real_t Mode7TileMapLayer::get_mode7_follow_horizon_tilt_offset() const {
	if (_mode7_display) {
		return _mode7_display->get_mode7_follow_horizon_tilt_offset();
	}
	return 0.0f;
}

void Mode7TileMapLayer::set_mode7_region_follow_target(const NodePath &p_path) {
	if (_mode7_display) {
		_mode7_display->set_mode7_region_follow_target(p_path);
	}
}

NodePath Mode7TileMapLayer::get_mode7_region_follow_target() const {
	if (_mode7_display) {
		return _mode7_display->get_mode7_region_follow_target();
	}
	return NodePath();
}

/*************************************************************************/
/* NEW BAKE-CONTROL PROPERTIES                                           */
/*************************************************************************/

void Mode7TileMapLayer::set_mode7_bake_mode(BakeMode p_mode) {
	if (mode7_bake_mode == p_mode) {
		return;
	}
	mode7_bake_mode = p_mode;

	// When switching to EVERY_FRAME, set viewport to continuous update and bake.
	if (p_mode == BakeMode::BAKE_MODE_EVERY_FRAME && is_inside_tree() && is_mode7_enabled()) {
		_mode7_perform_bake();
	} else if (p_mode != BakeMode::BAKE_MODE_EVERY_FRAME && _mode7_bake_viewport) {
		_mode7_bake_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
	}

	notify_property_list_changed();
}

Mode7TileMapLayer::BakeMode Mode7TileMapLayer::get_mode7_bake_mode() const {
	return mode7_bake_mode;
}

void Mode7TileMapLayer::set_mode7_bake_resolution_scale(real_t p_scale) {
	if (mode7_bake_resolution_scale == p_scale) {
		return;
	}
	mode7_bake_resolution_scale = CLAMP(p_scale, 0.1f, 4.0f);
	// Changing the scale changes the bake dimensions - trigger rebake.
	if (is_inside_tree()) {
		_mode7_trigger_rebake();
	}
}

real_t Mode7TileMapLayer::get_mode7_bake_resolution_scale() const {
	return mode7_bake_resolution_scale;
}

void Mode7TileMapLayer::set_mode7_bake_margin(int p_cells) {
	if (mode7_bake_margin == p_cells) {
		return;
	}
	mode7_bake_margin = CLAMP(p_cells, 0, 64);
	// Changing the margin changes the encompassing rect - trigger rebake.
	if (is_inside_tree()) {
		_mode7_trigger_rebake();
	}
}

int Mode7TileMapLayer::get_mode7_bake_margin() const {
	return mode7_bake_margin;
}

/*************************************************************************/
/* BINDING                                                               */
/*************************************************************************/

void Mode7TileMapLayer::_bind_methods() {
	// Forwarded Mode 7 properties (proxied to internal Mode7Sprite2D display child)
	ClassDB::bind_method(D_METHOD("set_mode7_enabled", "enabled"), &Mode7TileMapLayer::set_mode7_enabled);
	ClassDB::bind_method(D_METHOD("is_mode7_enabled"), &Mode7TileMapLayer::is_mode7_enabled);

	ClassDB::bind_method(D_METHOD("set_mode7_scanline_overrides", "overrides"), &Mode7TileMapLayer::set_mode7_scanline_overrides);
	ClassDB::bind_method(D_METHOD("get_mode7_scanline_overrides"), &Mode7TileMapLayer::get_mode7_scanline_overrides);

	ClassDB::bind_method(D_METHOD("set_mode7_tiling", "tiling"), &Mode7TileMapLayer::set_mode7_tiling);
	ClassDB::bind_method(D_METHOD("is_mode7_tiling"), &Mode7TileMapLayer::is_mode7_tiling);

	ClassDB::bind_method(D_METHOD("set_mode7_global_rotation", "radians"), &Mode7TileMapLayer::set_mode7_global_rotation);
	ClassDB::bind_method(D_METHOD("get_mode7_global_rotation"), &Mode7TileMapLayer::get_mode7_global_rotation);
	ClassDB::bind_method(D_METHOD("set_mode7_global_pivot", "pivot"), &Mode7TileMapLayer::set_mode7_global_pivot);
	ClassDB::bind_method(D_METHOD("get_mode7_global_pivot"), &Mode7TileMapLayer::get_mode7_global_pivot);

	// Region follow target
	ClassDB::bind_method(D_METHOD("set_mode7_region_follow_target", "path"), &Mode7TileMapLayer::set_mode7_region_follow_target);
	ClassDB::bind_method(D_METHOD("get_mode7_region_follow_target"), &Mode7TileMapLayer::get_mode7_region_follow_target);

	// Top horizon mask
	ClassDB::bind_method(D_METHOD("set_mode7_top_horizon_mask_amount", "amount"), &Mode7TileMapLayer::set_mode7_top_horizon_mask_amount);
	ClassDB::bind_method(D_METHOD("get_mode7_top_horizon_mask_amount"), &Mode7TileMapLayer::get_mode7_top_horizon_mask_amount);
	ClassDB::bind_method(D_METHOD("set_mode7_top_horizon_mask_tilt", "radians"), &Mode7TileMapLayer::set_mode7_top_horizon_mask_tilt);
	ClassDB::bind_method(D_METHOD("get_mode7_top_horizon_mask_tilt"), &Mode7TileMapLayer::get_mode7_top_horizon_mask_tilt);

	// Bottom horizon mask
	ClassDB::bind_method(D_METHOD("set_mode7_bottom_horizon_mask_amount", "amount"), &Mode7TileMapLayer::set_mode7_bottom_horizon_mask_amount);
	ClassDB::bind_method(D_METHOD("get_mode7_bottom_horizon_mask_amount"), &Mode7TileMapLayer::get_mode7_bottom_horizon_mask_amount);
	ClassDB::bind_method(D_METHOD("set_mode7_bottom_horizon_mask_tilt", "radians"), &Mode7TileMapLayer::set_mode7_bottom_horizon_mask_tilt);
	ClassDB::bind_method(D_METHOD("get_mode7_bottom_horizon_mask_tilt"), &Mode7TileMapLayer::get_mode7_bottom_horizon_mask_tilt);

	// Follow horizon tilts
	ClassDB::bind_method(D_METHOD("set_mode7_follow_horizon_tilts", "enabled"), &Mode7TileMapLayer::set_mode7_follow_horizon_tilts);
	ClassDB::bind_method(D_METHOD("is_mode7_follow_horizon_tilts"), &Mode7TileMapLayer::is_mode7_follow_horizon_tilts);
	ClassDB::bind_method(D_METHOD("set_mode7_follow_horizon_tilt_offset", "radians"), &Mode7TileMapLayer::set_mode7_follow_horizon_tilt_offset);
	ClassDB::bind_method(D_METHOD("get_mode7_follow_horizon_tilt_offset"), &Mode7TileMapLayer::get_mode7_follow_horizon_tilt_offset);

	// Bake control (new, specific to Mode7TileMapLayer)
	ClassDB::bind_method(D_METHOD("set_mode7_bake_mode", "mode"), &Mode7TileMapLayer::set_mode7_bake_mode);
	ClassDB::bind_method(D_METHOD("get_mode7_bake_mode"), &Mode7TileMapLayer::get_mode7_bake_mode);
	ClassDB::bind_method(D_METHOD("set_mode7_bake_resolution_scale", "scale"), &Mode7TileMapLayer::set_mode7_bake_resolution_scale);
	ClassDB::bind_method(D_METHOD("get_mode7_bake_resolution_scale"), &Mode7TileMapLayer::get_mode7_bake_resolution_scale);
	ClassDB::bind_method(D_METHOD("set_mode7_bake_margin", "cells"), &Mode7TileMapLayer::set_mode7_bake_margin);
	ClassDB::bind_method(D_METHOD("get_mode7_bake_margin"), &Mode7TileMapLayer::get_mode7_bake_margin);

	// Public script-callable bake method (available regardless of bake_mode)
	ClassDB::bind_method(D_METHOD("bake_mode7_texture"), &Mode7TileMapLayer::bake_mode7_texture);

	ADD_GROUP("Mode 7", "mode7_");

	// Bake control properties
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode7_bake_mode", PROPERTY_HINT_ENUM, "Manual,OnChange,EveryFrame"),
			"set_mode7_bake_mode", "get_mode7_bake_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mode7_bake_resolution_scale",
							 PROPERTY_HINT_RANGE, "0.1,4,0.05"),
			"set_mode7_bake_resolution_scale", "get_mode7_bake_resolution_scale");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode7_bake_margin",
							 PROPERTY_HINT_RANGE, "0,64,1"),
			"set_mode7_bake_margin", "get_mode7_bake_margin");

	// Forwarded properties (same names and hints as Mode7Sprite2D)
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "mode7_tiling"), "set_mode7_tiling", "is_mode7_tiling");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mode7_global_rotation",
							 PROPERTY_HINT_RANGE, "-360,360,0.1,radians_as_degrees"),
			"set_mode7_global_rotation", "get_mode7_global_rotation");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "mode7_global_pivot"),
			"set_mode7_global_pivot", "get_mode7_global_pivot");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "mode7_region_follow_target",
				PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D"),
			"set_mode7_region_follow_target", "get_mode7_region_follow_target");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mode7_top_horizon_mask_amount",
							 PROPERTY_HINT_RANGE, "0,1,0.001"),
			"set_mode7_top_horizon_mask_amount", "get_mode7_top_horizon_mask_amount");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mode7_top_horizon_mask_tilt",
							 PROPERTY_HINT_RANGE, "-360,360,0.1,radians_as_degrees"),
			"set_mode7_top_horizon_mask_tilt", "get_mode7_top_horizon_mask_tilt");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mode7_bottom_horizon_mask_amount",
							 PROPERTY_HINT_RANGE, "0,1,0.001"),
			"set_mode7_bottom_horizon_mask_amount", "get_mode7_bottom_horizon_mask_amount");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mode7_bottom_horizon_mask_tilt",
					 PROPERTY_HINT_RANGE, "-360,360,0.1,radians_as_degrees"),
		"set_mode7_bottom_horizon_mask_tilt", "get_mode7_bottom_horizon_mask_tilt");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "mode7_follow_horizon_tilts"),
		"set_mode7_follow_horizon_tilts", "is_mode7_follow_horizon_tilts");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mode7_follow_horizon_tilt_offset",
					 PROPERTY_HINT_RANGE, "-360,360,0.1,radians_as_degrees"),
		"set_mode7_follow_horizon_tilt_offset", "get_mode7_follow_horizon_tilt_offset");

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "mode7_enabled", PROPERTY_HINT_GROUP_ENABLE),
			"set_mode7_enabled", "is_mode7_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "mode7_scanline_overrides",
							 PROPERTY_HINT_ARRAY_TYPE, "Mode7ScanlineOverride"),
			"set_mode7_scanline_overrides", "get_mode7_scanline_overrides");
}
