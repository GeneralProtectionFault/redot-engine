/**************************************************************************/
/*  mode7_tile_map_layer.h                                                */
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

#pragma once

#include "scene/2d/mode7_sprite_2d.h"
#include "scene/2d/tile_map_layer.h"
#include "scene/main/viewport.h"
#include "scene/resources/image_texture.h"

class Mode7TileMapLayer : public TileMapLayer {
	GDCLASS(Mode7TileMapLayer, TileMapLayer);

private:
	/// @name Internal composition
	/// @{
	SubViewport *_mode7_bake_viewport = nullptr;
	Mode7Sprite2D *_mode7_display = nullptr;
	Ref<ImageTexture> _mode7_baked_texture;
	bool _mode7_pending_bake = false;
	/// @}

	/// @name Bake control
	/// @{
	enum BakeMode {
		BAKE_MODE_MANUAL,     // Only bake on explicit call to bake_mode7_texture()
		BAKE_MODE_ON_CHANGE,  // Auto-bake (debounced) when tile data changes
		BAKE_MODE_EVERY_FRAME // Live ViewportTexture - no snapshotting
	};

	BakeMode mode7_bake_mode = BAKE_MODE_MANUAL;
	real_t   mode7_bake_resolution_scale = 1.0f;
	int      mode7_bake_margin = 0;
	/// @}

	void _mode7_compute_encompassing(Rect2 &r_rect) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

	// Internal helpers
	void _mode7_setup_composition();
	void _mode7_perform_bake();
	void _mode7_on_bake_frame_drawn();
	void _mode7_trigger_rebake();
	void _mode7_deferred_rebake();
	void _mode7_cleanup();

	// Tile data change handler (debounced)
	void _on_tilemap_changed();

public:
	/// @name Forwarded Mode 7 properties (proxied to _mode7_display)
	/// @{
	void set_mode7_enabled(bool p_enabled);
	bool is_mode7_enabled() const;

	void set_mode7_scanline_overrides(const TypedArray<Mode7ScanlineOverride> &p_overrides);
	TypedArray<Mode7ScanlineOverride> get_mode7_scanline_overrides() const;

	void set_mode7_tiling(bool p_tiling);
	bool is_mode7_tiling() const;

	void set_mode7_global_rotation(real_t p_radians);
	real_t get_mode7_global_rotation() const;

	void set_mode7_global_pivot(const Vector2 &p_pivot);
	Vector2 get_mode7_global_pivot() const;

	void set_mode7_top_horizon_mask_amount(real_t p_amount);
	real_t get_mode7_top_horizon_mask_amount() const;

	void set_mode7_top_horizon_mask_tilt(real_t p_radians);
	real_t get_mode7_top_horizon_mask_tilt() const;

	void set_mode7_bottom_horizon_mask_amount(real_t p_amount);
	real_t get_mode7_bottom_horizon_mask_amount() const;

	void set_mode7_bottom_horizon_mask_tilt(real_t p_radians);
	real_t get_mode7_bottom_horizon_mask_tilt() const;

	void set_mode7_follow_horizon_tilts(bool p_enabled);
	bool is_mode7_follow_horizon_tilts() const;

	void set_mode7_follow_horizon_tilt_offset(real_t p_radians);
	real_t get_mode7_follow_horizon_tilt_offset() const;

	void set_mode7_region_follow_target(const NodePath &p_path);
	NodePath get_mode7_region_follow_target() const;
	/// @}

	/// @name Bake control properties (new)
	/// @{
	void set_mode7_bake_mode(BakeMode p_mode);
	BakeMode get_mode7_bake_mode() const;

	void set_mode7_bake_resolution_scale(real_t p_scale);
	real_t get_mode7_bake_resolution_scale() const;

	void set_mode7_bake_margin(int p_cells);
	int get_mode7_bake_margin() const;
	/// @}

	// Script-callable - bake regardless of mode7_bake_mode
	void bake_mode7_texture();

	Mode7TileMapLayer();
	~Mode7TileMapLayer();
};

VARIANT_ENUM_CAST(Mode7TileMapLayer::BakeMode);
