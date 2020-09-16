/*
 * Modern effects for a modern Streamer
 * Copyright (C) 2017-2018 Michael Fabian Dirks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "filter-taa.hpp"
#include "strings.hpp"
#include <stdexcept>
#include "obs/gs/gs-helper.hpp"

#define LOG_PREFIX "<filter-taa> "

// Translation Strings
#define ST "Filter.TAA"

#define ST_TAA "Filter.TAA.TAA"
#define ST_TAA_RANGE_MINIMUM "Filter.TAA.TAA.Range.Minimum"
#define ST_TAA_RANGE_MAXIMUM "Filter.TAA.TAA.Range.Maximum"
#define ST_TAA_OFFSET_X "Filter.TAA.TAA.Offset.X"
#define ST_TAA_OFFSET_Y "Filter.TAA.TAA.Offset.Y"
#define ST_TAA_COLOR "Filter.TAA.TAA.Color"
#define ST_TAA_ALPHA "Filter.TAA.TAA.Alpha"

#define ST_SHADOW_INNER "Filter.TAA.Shadow.Inner"
#define ST_SHADOW_INNER_RANGE_MINIMUM "Filter.TAA.Shadow.Inner.Range.Minimum"
#define ST_SHADOW_INNER_RANGE_MAXIMUM "Filter.TAA.Shadow.Inner.Range.Maximum"
#define ST_SHADOW_INNER_OFFSET_X "Filter.TAA.Shadow.Inner.Offset.X"
#define ST_SHADOW_INNER_OFFSET_Y "Filter.TAA.Shadow.Inner.Offset.Y"
#define ST_SHADOW_INNER_COLOR "Filter.TAA.Shadow.Inner.Color"
#define ST_SHADOW_INNER_ALPHA "Filter.TAA.Shadow.Inner.Alpha"

#define ST_SHADOW_OUTER "Filter.TAA.Shadow.Outer"
#define ST_SHADOW_OUTER_RANGE_MINIMUM "Filter.TAA.Shadow.Outer.Range.Minimum"
#define ST_SHADOW_OUTER_RANGE_MAXIMUM "Filter.TAA.Shadow.Outer.Range.Maximum"
#define ST_SHADOW_OUTER_OFFSET_X "Filter.TAA.Shadow.Outer.Offset.X"
#define ST_SHADOW_OUTER_OFFSET_Y "Filter.TAA.Shadow.Outer.Offset.Y"
#define ST_SHADOW_OUTER_COLOR "Filter.TAA.Shadow.Outer.Color"
#define ST_SHADOW_OUTER_ALPHA "Filter.TAA.Shadow.Outer.Alpha"

#define ST_GLOW_INNER "Filter.TAA.Glow.Inner"
#define ST_GLOW_INNER_COLOR "Filter.TAA.Glow.Inner.Color"
#define ST_GLOW_INNER_ALPHA "Filter.TAA.Glow.Inner.Alpha"
#define ST_GLOW_INNER_WIDTH "Filter.TAA.Glow.Inner.Width"
#define ST_GLOW_INNER_SHARPNESS "Filter.TAA.Glow.Inner.Sharpness"

#define ST_GLOW_OUTER "Filter.TAA.Glow.Outer"
#define ST_GLOW_OUTER_COLOR "Filter.TAA.Glow.Outer.Color"
#define ST_GLOW_OUTER_ALPHA "Filter.TAA.Glow.Outer.Alpha"
#define ST_GLOW_OUTER_WIDTH "Filter.TAA.Glow.Outer.Width"
#define ST_GLOW_OUTER_SHARPNESS "Filter.TAA.Glow.Outer.Sharpness"

#define ST_OUTLINE "Filter.TAA.Outline"
#define ST_OUTLINE_COLOR "Filter.TAA.Outline.Color"
#define ST_OUTLINE_ALPHA "Filter.TAA.Outline.Alpha"
#define ST_OUTLINE_WIDTH "Filter.TAA.Outline.Width"
#define ST_OUTLINE_OFFSET "Filter.TAA.Outline.Offset"
#define ST_OUTLINE_SHARPNESS "Filter.TAA.Outline.Sharpness"

#define ST_SDF_SCALE "Filter.TAA.SDF.Scale"
#define ST_SDF_THRESHOLD "Filter.TAA.SDF.Threshold"

using namespace streamfx::filter::taa;

taa_instance::taa_instance(obs_data_t* settings, obs_source_t* self)
	: obs::source_instance(settings, self), _source_rendered(false), _taa_scale(1.0), _taa_threshold(),
	  _output_rendered(false), _time(0), _time_loop(0), _loops(0), _taa(false), _taa_color(), _taa_range_min(),
	  _taa_range_max(), _taa_offset_x(), _taa_offset_y(), _inner_shadow(false), _inner_shadow_color(),
	  _inner_shadow_range_min(), _inner_shadow_range_max(), _inner_shadow_offset_x(), _inner_shadow_offset_y(),
	  _outer_shadow(false), _outer_shadow_color(), _outer_shadow_range_min(), _outer_shadow_range_max(),
	  _outer_shadow_offset_x(), _outer_shadow_offset_y(), _inner_glow(false), _inner_glow_color(), _inner_glow_width(),
	  _inner_glow_sharpness(), _inner_glow_sharpness_inv(), _outer_glow(false), _outer_glow_color(),
	  _outer_glow_width(), _outer_glow_sharpness(), _outer_glow_sharpness_inv(), _outline(false), _outline_color(),
	  _outline_width(), _outline_offset(), _outline_sharpness(), _outline_sharpness_inv()
{
	{
		auto gctx        = gs::context();
		vec4 transparent = {0, 0, 0, 0};

		_source_rt = std::make_shared<gs::rendertarget>(GS_RGBA, GS_ZS_NONE);
		_taa_write = std::make_shared<gs::rendertarget>(GS_RGBA32F, GS_ZS_NONE);
		_taa_read  = std::make_shared<gs::rendertarget>(GS_RGBA32F, GS_ZS_NONE);
		_output_rt = std::make_shared<gs::rendertarget>(GS_RGBA, GS_ZS_NONE);

		std::shared_ptr<gs::rendertarget> initialize_rts[] = {_source_rt, _taa_write, _taa_read, _output_rt};
		for (auto rt : initialize_rts) {
			auto op = rt->render(1, 1);
			gs_clear(GS_CLEAR_COLOR | GS_CLEAR_DEPTH, &transparent, 0, 0);
		}

		std::pair<const char*, gs::effect&> load_arr[] = {
			{"effects/taa/taa-producer.effect", _taa_producer_effect},
			{"effects/taa/taa-consumer.effect", _taa_consumer_effect},
		};
		for (auto& kv : load_arr) {
			char* path = obs_module_file(kv.first);
			if (!path) {
				DLOG_ERROR(LOG_PREFIX "Unable to load _effect '%s' as file is missing or locked.", kv.first);
				continue;
			}
			try {
				kv.second = gs::effect::create(path);
			} catch (const std::exception& ex) {
				DLOG_ERROR(LOG_PREFIX "Failed to load _effect '%s' (located at '%s') with error(s): %s", kv.first, path,
						   ex.what());
			}
			bfree(path);
		}
	}

	update(settings);
}

taa_instance::~taa_instance() {}

void taa_instance::load(obs_data_t* settings)
{
	update(settings);
}

void taa_instance::migrate(obs_data_t* data, uint64_t version) {}

void taa_instance::update(obs_data_t* data)
{
	{
		_taa = obs_data_get_bool(data, ST_TAA)
			   && (obs_data_get_double(data, ST_TAA_ALPHA) >= std::numeric_limits<double_t>::epsilon());
		{
			struct cs {
				uint8_t r, g, b, a;
			};
			union {
				uint32_t color;
				uint8_t  channel[4];
				cs       c;
			};
			color        = uint32_t(obs_data_get_int(data, ST_TAA_COLOR));
			_taa_color.x = float_t(c.r / 255.0);
			_taa_color.y = float_t(c.g / 255.0);
			_taa_color.z = float_t(c.b / 255.0);
			_taa_color.w = float_t(obs_data_get_double(data, ST_TAA_ALPHA) / 100.0);
		}
		_taa_range_min = float_t(obs_data_get_double(data, ST_TAA_RANGE_MINIMUM));
		_taa_range_max = float_t(obs_data_get_double(data, ST_TAA_RANGE_MAXIMUM));
		_taa_offset_x  = float_t(obs_data_get_double(data, ST_TAA_OFFSET_X));
		_taa_offset_y  = float_t(obs_data_get_double(data, ST_TAA_OFFSET_Y));
	}

	{
		_outer_shadow =
			obs_data_get_bool(data, ST_SHADOW_OUTER)
			&& (obs_data_get_double(data, ST_SHADOW_OUTER_ALPHA) >= std::numeric_limits<double_t>::epsilon());
		{
			struct cs {
				uint8_t r, g, b, a;
			};
			union {
				uint32_t color;
				uint8_t  channel[4];
				cs       c;
			};
			color                 = uint32_t(obs_data_get_int(data, ST_SHADOW_OUTER_COLOR));
			_outer_shadow_color.x = float_t(c.r / 255.0);
			_outer_shadow_color.y = float_t(c.g / 255.0);
			_outer_shadow_color.z = float_t(c.b / 255.0);
			_outer_shadow_color.w = float_t(obs_data_get_double(data, ST_SHADOW_OUTER_ALPHA) / 100.0);
		}
		_outer_shadow_range_min = float_t(obs_data_get_double(data, ST_SHADOW_OUTER_RANGE_MINIMUM));
		_outer_shadow_range_max = float_t(obs_data_get_double(data, ST_SHADOW_OUTER_RANGE_MAXIMUM));
		_outer_shadow_offset_x  = float_t(obs_data_get_double(data, ST_SHADOW_OUTER_OFFSET_X));
		_outer_shadow_offset_y  = float_t(obs_data_get_double(data, ST_SHADOW_OUTER_OFFSET_Y));
	}

	{
		_inner_shadow =
			obs_data_get_bool(data, ST_SHADOW_INNER)
			&& (obs_data_get_double(data, ST_SHADOW_INNER_ALPHA) >= std::numeric_limits<double_t>::epsilon());
		{
			struct cs {
				uint8_t r, g, b, a;
			};
			union {
				uint32_t color;
				uint8_t  channel[4];
				cs       c;
			};
			color                 = uint32_t(obs_data_get_int(data, ST_SHADOW_INNER_COLOR));
			_inner_shadow_color.x = float_t(c.r / 255.0);
			_inner_shadow_color.y = float_t(c.g / 255.0);
			_inner_shadow_color.z = float_t(c.b / 255.0);
			_inner_shadow_color.w = float_t(obs_data_get_double(data, ST_SHADOW_INNER_ALPHA) / 100.0);
		}
		_inner_shadow_range_min = float_t(obs_data_get_double(data, ST_SHADOW_INNER_RANGE_MINIMUM));
		_inner_shadow_range_max = float_t(obs_data_get_double(data, ST_SHADOW_INNER_RANGE_MAXIMUM));
		_inner_shadow_offset_x  = float_t(obs_data_get_double(data, ST_SHADOW_INNER_OFFSET_X));
		_inner_shadow_offset_y  = float_t(obs_data_get_double(data, ST_SHADOW_INNER_OFFSET_Y));
	}

	{
		_outer_glow = obs_data_get_bool(data, ST_GLOW_OUTER)
					  && (obs_data_get_double(data, ST_GLOW_OUTER_ALPHA) >= std::numeric_limits<double_t>::epsilon());
		{
			struct cs {
				uint8_t r, g, b, a;
			};
			union {
				uint32_t color;
				uint8_t  channel[4];
				cs       c;
			};
			color               = uint32_t(obs_data_get_int(data, ST_GLOW_OUTER_COLOR));
			_outer_glow_color.x = float_t(c.r / 255.0);
			_outer_glow_color.y = float_t(c.g / 255.0);
			_outer_glow_color.z = float_t(c.b / 255.0);
			_outer_glow_color.w = float_t(obs_data_get_double(data, ST_GLOW_OUTER_ALPHA) / 100.0);
		}
		_outer_glow_width         = float_t(obs_data_get_double(data, ST_GLOW_OUTER_WIDTH));
		_outer_glow_sharpness     = float_t(obs_data_get_double(data, ST_GLOW_OUTER_SHARPNESS) / 100.0);
		_outer_glow_sharpness_inv = float_t(1.0f / (1.0f - _outer_glow_sharpness));
		if (_outer_glow_sharpness >= (1.0f - std::numeric_limits<float_t>::epsilon())) {
			_outer_glow_sharpness = 1.0f - std::numeric_limits<float_t>::epsilon();
		}
	}

	{
		_inner_glow = obs_data_get_bool(data, ST_GLOW_INNER)
					  && (obs_data_get_double(data, ST_GLOW_INNER_ALPHA) >= std::numeric_limits<double_t>::epsilon());
		{
			struct cs {
				uint8_t r, g, b, a;
			};
			union {
				uint32_t color;
				uint8_t  channel[4];
				cs       c;
			};
			color               = uint32_t(obs_data_get_int(data, ST_GLOW_INNER_COLOR));
			_inner_glow_color.x = float_t(c.r / 255.0);
			_inner_glow_color.y = float_t(c.g / 255.0);
			_inner_glow_color.z = float_t(c.b / 255.0);
			_inner_glow_color.w = float_t(obs_data_get_double(data, ST_GLOW_INNER_ALPHA) / 100.0);
		}
		_inner_glow_width         = float_t(obs_data_get_double(data, ST_GLOW_INNER_WIDTH));
		_inner_glow_sharpness     = float_t(obs_data_get_double(data, ST_GLOW_INNER_SHARPNESS) / 100.0);
		_inner_glow_sharpness_inv = float_t(1.0f / (1.0f - _inner_glow_sharpness));
		if (_inner_glow_sharpness >= (1.0f - std::numeric_limits<float_t>::epsilon())) {
			_inner_glow_sharpness = 1.0f - std::numeric_limits<float_t>::epsilon();
		}
	}

	{
		_outline = obs_data_get_bool(data, ST_OUTLINE)
				   && (obs_data_get_double(data, ST_OUTLINE_ALPHA) >= std::numeric_limits<double_t>::epsilon());
		{
			struct cs {
				uint8_t r, g, b, a;
			};
			union {
				uint32_t color;
				uint8_t  channel[4];
				cs       c;
			};
			color            = uint32_t(obs_data_get_int(data, ST_OUTLINE_COLOR));
			_outline_color.x = float_t(c.r / 255.0);
			_outline_color.y = float_t(c.g / 255.0);
			_outline_color.z = float_t(c.b / 255.0);
			_outline_color.w = float_t(obs_data_get_double(data, ST_OUTLINE_ALPHA) / 100.0);
		}
		_outline_width         = float_t(obs_data_get_double(data, ST_OUTLINE_WIDTH));
		_outline_offset        = float_t(obs_data_get_double(data, ST_OUTLINE_OFFSET));
		_outline_sharpness     = float_t(obs_data_get_double(data, ST_OUTLINE_SHARPNESS) / 100.0);
		_outline_sharpness_inv = float_t(1.0f / (1.0f - _outline_sharpness));
		if (_outline_sharpness >= (1.0f - std::numeric_limits<float_t>::epsilon())) {
			_outline_sharpness = 1.0f - std::numeric_limits<float_t>::epsilon();
		}
	}

	_taa_scale     = double_t(obs_data_get_double(data, ST_SDF_SCALE) / 100.0);
	_taa_threshold = float_t(obs_data_get_double(data, ST_SDF_THRESHOLD) / 100.0);
}

void taa_instance::video_tick(float_t time)
{
	if (obs_source_t* target = obs_filter_get_target(_self); target != nullptr) {
		_source_rendered = false;
		_output_rendered = false;
	}

	// Update State
	_time += time;
	_time_loop += time;
	if (_time_loop > 1.) {
		_time_loop -= 1.;

		// Loops
		_loops += 1;
		if (_loops >= 4194304)
			_loops = -_loops;
	}
}

void taa_instance::video_render(gs_effect_t* effect)
{
	obs_source_t* parent         = obs_filter_get_parent(_self);
	obs_source_t* target         = obs_filter_get_target(_self);
	uint32_t      baseW          = obs_source_get_base_width(target);
	uint32_t      baseH          = obs_source_get_base_height(target);
	gs_effect_t*  final_effect   = effect ? effect : obs_get_base_effect(obs_base_effect::OBS_EFFECT_DEFAULT);
	gs_effect_t*  default_effect = obs_get_base_effect(obs_base_effect::OBS_EFFECT_DEFAULT);

	if (!_self || !parent || !target || !baseW || !baseH || !final_effect) {
		obs_source_skip_video_filter(_self);
		return;
	}

#ifdef ENABLE_PROFILING
	gs::debug_marker gdmp{gs::debug_color_source, "SDF Effects '%s' on '%s'", obs_source_get_name(_self),
						  obs_source_get_name(obs_filter_get_parent(_self))};
#endif

	auto gctx              = gs::context();
	vec4 color_transparent = {0, 0, 0, 0};

	try {
		gs_blend_state_push();
		gs_reset_blend_state();
		gs_enable_blending(false);
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		gs_set_cull_mode(GS_NEITHER);
		gs_enable_color(true, true, true, true);
		gs_enable_depth_test(false);
		gs_depth_function(GS_ALWAYS);
		gs_enable_stencil_test(false);
		gs_enable_stencil_write(false);
		gs_stencil_function(GS_STENCIL_BOTH, GS_ALWAYS);
		gs_stencil_op(GS_STENCIL_BOTH, GS_ZERO, GS_ZERO, GS_ZERO);

		if (!_source_rendered) {
			// Store input texture.
			{
#ifdef ENABLE_PROFILING
				gs::debug_marker gdm{gs::debug_color_cache, "Cache"};
#endif

				auto op = _source_rt->render(baseW, baseH);
				gs_ortho(0, static_cast<float>(baseW), 0, static_cast<float>(baseH), -1, 1);
				gs_clear(GS_CLEAR_COLOR | GS_CLEAR_DEPTH, &color_transparent, 0, 0);

				if (obs_source_process_filter_begin(_self, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING)) {
					obs_source_process_filter_end(_self, final_effect, baseW, baseH);
				} else {
					throw std::runtime_error("failed to process source");
				}
			}
			_source_rt->get_texture(_source_texture);
			if (!_source_texture) {
				throw std::runtime_error("failed to draw source");
			}

			// Generate SDF Buffers
			{
				_taa_read->get_texture(_taa_texture);
				if (!_taa_texture) {
					throw std::runtime_error("SDF Backbuffer empty");
				}

				if (!_taa_producer_effect) {
					throw std::runtime_error("SDF Effect no loaded");
				}

				// Scale SDF Size
				double_t taaW, taaH;
				taaW = baseW * _taa_scale;
				taaH = baseH * _taa_scale;
				if (taaW <= 1) {
					taaW = 1.0;
				}
				if (taaH <= 1) {
					taaH = 1.0;
				}

				{
#ifdef ENABLE_PROFILING
					gs::debug_marker gdm{gs::debug_color_convert, "Update Distance Field"};
#endif

					auto op = _taa_write->render(uint32_t(taaW), uint32_t(taaH));
					gs_ortho(0, 1, 0, 1, -1, 1);
					gs_clear(GS_CLEAR_COLOR | GS_CLEAR_DEPTH, &color_transparent, 0, 0);

					_taa_producer_effect.get_parameter("_image").set_texture(_source_texture);
					_taa_producer_effect.get_parameter("_size").set_float2(float_t(taaW), float_t(taaH));
					_taa_producer_effect.get_parameter("_taa").set_texture(_taa_texture);
					_taa_producer_effect.get_parameter("_threshold").set_float(_taa_threshold);

					while (gs_effect_loop(_taa_producer_effect.get_object(), "Draw")) {
						streamfx::gs_draw_fullscreen_tri();
					}
				}
				std::swap(_taa_read, _taa_write);
				_taa_read->get_texture(_taa_texture);
				if (!_taa_texture) {
					throw std::runtime_error("SDF Backbuffer empty");
				}
			}

			_source_rendered = true;
		}

		gs_blend_state_pop();
	} catch (...) {
		gs_blend_state_pop();
		obs_source_skip_video_filter(_self);
		return;
	}

	if (!_output_rendered) {
		_output_texture = _source_texture;

		if (!_taa_consumer_effect) {
			obs_source_skip_video_filter(_self);
			return;
		}

		gs_blend_state_push();
		gs_reset_blend_state();
		gs_enable_color(true, true, true, true);
		gs_enable_depth_test(false);
		gs_set_cull_mode(GS_NEITHER);

		// SDF Effects Stack:
		//   Normal Source
		//   Temporal Anti-Aliasing
		//   Outer Shadow
		//   Inner Shadow
		//   Outer Glow
		//   Inner Glow
		//   Outline

		// Optimized Render path.
		try {
#ifdef ENABLE_PROFILING
			gs::debug_marker gdm{gs::debug_color_convert, "Calculate"};
#endif

			auto op = _output_rt->render(baseW, baseH);
			gs_ortho(0, 1, 0, 1, 0, 1);

			gs_enable_blending(false);
			gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
			auto param = gs_effect_get_param_by_name(default_effect, "image");
			if (param) {
				gs_effect_set_texture(param, _output_texture->get_object());
			}
			while (gs_effect_loop(default_effect, "Draw")) {
				streamfx::gs_draw_fullscreen_tri();
			}

			gs_enable_blending(true);
			gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_ONE);
			if (_taa) {
				_taa_consumer_effect.get_parameter("pSDFTexture").set_texture(_taa_texture);
				_taa_consumer_effect.get_parameter("pSDFThreshold").set_float(_taa_threshold);
				_taa_consumer_effect.get_parameter("pImageTexture").set_texture(_source_texture->get_object());
				_taa_consumer_effect.get_parameter("pTAAColor").set_float4(_taa_color);
				_taa_consumer_effect.get_parameter("pTAAMin").set_float(_taa_range_min);
				_taa_consumer_effect.get_parameter("pTAAMax").set_float(_taa_range_max);
				_taa_consumer_effect.get_parameter("pTAAOffset")
					.set_float2(_taa_offset_x / float_t(baseW), _taa_offset_y / float_t(baseH));
				while (gs_effect_loop(_taa_consumer_effect.get_object(), "TAA")) {
					streamfx::gs_draw_fullscreen_tri();
				}
			}
			if (_outer_shadow) {
				_taa_consumer_effect.get_parameter("pSDFTexture").set_texture(_taa_texture);
				_taa_consumer_effect.get_parameter("pSDFThreshold").set_float(_taa_threshold);
				_taa_consumer_effect.get_parameter("pImageTexture").set_texture(_source_texture->get_object());
				_taa_consumer_effect.get_parameter("pShadowColor").set_float4(_outer_shadow_color);
				_taa_consumer_effect.get_parameter("pShadowMin").set_float(_outer_shadow_range_min);
				_taa_consumer_effect.get_parameter("pShadowMax").set_float(_outer_shadow_range_max);
				_taa_consumer_effect.get_parameter("pShadowOffset")
					.set_float2(_outer_shadow_offset_x / float_t(baseW), _outer_shadow_offset_y / float_t(baseH));
				while (gs_effect_loop(_taa_consumer_effect.get_object(), "ShadowOuter")) {
					streamfx::gs_draw_fullscreen_tri();
				}
			}
			if (_inner_shadow) {
				_taa_consumer_effect.get_parameter("pSDFTexture").set_texture(_taa_texture);
				_taa_consumer_effect.get_parameter("pSDFThreshold").set_float(_taa_threshold);
				_taa_consumer_effect.get_parameter("pImageTexture").set_texture(_source_texture->get_object());
				_taa_consumer_effect.get_parameter("pShadowColor").set_float4(_inner_shadow_color);
				_taa_consumer_effect.get_parameter("pShadowMin").set_float(_inner_shadow_range_min);
				_taa_consumer_effect.get_parameter("pShadowMax").set_float(_inner_shadow_range_max);
				_taa_consumer_effect.get_parameter("pShadowOffset")
					.set_float2(_inner_shadow_offset_x / float_t(baseW), _inner_shadow_offset_y / float_t(baseH));
				while (gs_effect_loop(_taa_consumer_effect.get_object(), "ShadowInner")) {
					streamfx::gs_draw_fullscreen_tri();
				}
			}
			if (_outer_glow) {
				_taa_consumer_effect.get_parameter("pSDFTexture").set_texture(_taa_texture);
				_taa_consumer_effect.get_parameter("pSDFThreshold").set_float(_taa_threshold);
				_taa_consumer_effect.get_parameter("pImageTexture").set_texture(_source_texture->get_object());
				_taa_consumer_effect.get_parameter("pGlowColor").set_float4(_outer_glow_color);
				_taa_consumer_effect.get_parameter("pGlowWidth").set_float(_outer_glow_width);
				_taa_consumer_effect.get_parameter("pGlowSharpness").set_float(_outer_glow_sharpness);
				_taa_consumer_effect.get_parameter("pGlowSharpnessInverse").set_float(_outer_glow_sharpness_inv);
				while (gs_effect_loop(_taa_consumer_effect.get_object(), "GlowOuter")) {
					streamfx::gs_draw_fullscreen_tri();
				}
			}
			if (_inner_glow) {
				_taa_consumer_effect.get_parameter("pSDFTexture").set_texture(_taa_texture);
				_taa_consumer_effect.get_parameter("pSDFThreshold").set_float(_taa_threshold);
				_taa_consumer_effect.get_parameter("pImageTexture").set_texture(_source_texture->get_object());
				_taa_consumer_effect.get_parameter("pGlowColor").set_float4(_inner_glow_color);
				_taa_consumer_effect.get_parameter("pGlowWidth").set_float(_inner_glow_width);
				_taa_consumer_effect.get_parameter("pGlowSharpness").set_float(_inner_glow_sharpness);
				_taa_consumer_effect.get_parameter("pGlowSharpnessInverse").set_float(_inner_glow_sharpness_inv);
				while (gs_effect_loop(_taa_consumer_effect.get_object(), "GlowInner")) {
					streamfx::gs_draw_fullscreen_tri();
				}
			}
			if (_outline) {
				_taa_consumer_effect.get_parameter("pSDFTexture").set_texture(_taa_texture);
				_taa_consumer_effect.get_parameter("pSDFThreshold").set_float(_taa_threshold);
				_taa_consumer_effect.get_parameter("pImageTexture").set_texture(_source_texture->get_object());
				_taa_consumer_effect.get_parameter("pOutlineColor").set_float4(_outline_color);
				_taa_consumer_effect.get_parameter("pOutlineWidth").set_float(_outline_width);
				_taa_consumer_effect.get_parameter("pOutlineOffset").set_float(_outline_offset);
				_taa_consumer_effect.get_parameter("pOutlineSharpness").set_float(_outline_sharpness);
				_taa_consumer_effect.get_parameter("pOutlineSharpnessInverse").set_float(_outline_sharpness_inv);
				while (gs_effect_loop(_taa_consumer_effect.get_object(), "Outline")) {
					streamfx::gs_draw_fullscreen_tri();
				}
			}
		} catch (...) {
		}

		_output_rt->get_texture(_output_texture);

		gs_blend_state_pop();
		_output_rendered = true;
	}

	if (!_output_texture) {
		obs_source_skip_video_filter(_self);
		return;
	}

	{
#ifdef ENABLE_PROFILING
		gs::debug_marker gdm{gs::debug_color_render, "Render"};
#endif

		gs_eparam_t* ep = gs_effect_get_param_by_name(final_effect, "image");
		if (ep) {
			gs_effect_set_texture(ep, _output_texture->get_object());
		}
		while (gs_effect_loop(final_effect, "Draw")) {
			gs_draw_sprite(0, 0, baseW, baseH);
		}
	}
}

taa_factory::taa_factory()
{
	_info.id           = PREFIX "filter-taa";
	_info.type         = OBS_SOURCE_TYPE_FILTER;
	_info.output_flags = OBS_SOURCE_VIDEO;

	set_resolution_enabled(false);
	finish_setup();
	register_proxy("obs-stream-effects-filter-taa");
}

taa_factory::~taa_factory() {}

const char* taa_factory::get_name()
{
	return D_TRANSLATE(ST);
}

void taa_factory::get_defaults2(obs_data_t* data)
{
	obs_data_set_default_bool(data, ST_TAA, false);
	obs_data_set_default_int(data, ST_TAA_COLOR, 0x00000000);
	obs_data_set_default_double(data, ST_TAA_ALPHA, 100.0);
	obs_data_set_default_double(data, ST_TAA_RANGE_MINIMUM, 0.0);
	obs_data_set_default_double(data, ST_TAA_RANGE_MAXIMUM, 4.0);
	obs_data_set_default_double(data, ST_TAA_OFFSET_X, 0.0);
	obs_data_set_default_double(data, ST_TAA_OFFSET_Y, 0.0);

	obs_data_set_default_bool(data, ST_SHADOW_OUTER, false);
	obs_data_set_default_int(data, ST_SHADOW_OUTER_COLOR, 0x00000000);
	obs_data_set_default_double(data, ST_SHADOW_OUTER_ALPHA, 100.0);
	obs_data_set_default_double(data, ST_SHADOW_OUTER_RANGE_MINIMUM, 0.0);
	obs_data_set_default_double(data, ST_SHADOW_OUTER_RANGE_MAXIMUM, 4.0);
	obs_data_set_default_double(data, ST_SHADOW_OUTER_OFFSET_X, 0.0);
	obs_data_set_default_double(data, ST_SHADOW_OUTER_OFFSET_Y, 0.0);

	obs_data_set_default_bool(data, ST_SHADOW_INNER, false);
	obs_data_set_default_int(data, ST_SHADOW_INNER_COLOR, 0x00000000);
	obs_data_set_default_double(data, ST_SHADOW_INNER_ALPHA, 100.0);
	obs_data_set_default_double(data, ST_SHADOW_INNER_RANGE_MINIMUM, 0.0);
	obs_data_set_default_double(data, ST_SHADOW_INNER_RANGE_MAXIMUM, 4.0);
	obs_data_set_default_double(data, ST_SHADOW_INNER_OFFSET_X, 0.0);
	obs_data_set_default_double(data, ST_SHADOW_INNER_OFFSET_Y, 0.0);

	obs_data_set_default_bool(data, ST_GLOW_OUTER, false);
	obs_data_set_default_int(data, ST_GLOW_OUTER_COLOR, 0xFFFFFFFF);
	obs_data_set_default_double(data, ST_GLOW_OUTER_ALPHA, 100.0);
	obs_data_set_default_double(data, ST_GLOW_OUTER_WIDTH, 4.0);
	obs_data_set_default_double(data, ST_GLOW_OUTER_SHARPNESS, 50.0);

	obs_data_set_default_bool(data, ST_GLOW_INNER, false);
	obs_data_set_default_int(data, ST_GLOW_INNER_COLOR, 0xFFFFFFFF);
	obs_data_set_default_double(data, ST_GLOW_INNER_ALPHA, 100.0);
	obs_data_set_default_double(data, ST_GLOW_INNER_WIDTH, 4.0);
	obs_data_set_default_double(data, ST_GLOW_INNER_SHARPNESS, 50.0);

	obs_data_set_default_bool(data, ST_OUTLINE, false);
	obs_data_set_default_int(data, ST_OUTLINE_COLOR, 0x00000000);
	obs_data_set_default_double(data, ST_OUTLINE_ALPHA, 100.0);
	obs_data_set_default_double(data, ST_OUTLINE_WIDTH, 4.0);
	obs_data_set_default_double(data, ST_OUTLINE_OFFSET, 0.0);
	obs_data_set_default_double(data, ST_OUTLINE_SHARPNESS, 50.0);

	obs_data_set_default_bool(data, S_ADVANCED, false);
	obs_data_set_default_double(data, ST_SDF_SCALE, 100.0);
	obs_data_set_default_double(data, ST_SDF_THRESHOLD, 50.0);
}

bool cb_modified_taa(void*, obs_properties_t* props, obs_property*, obs_data_t* settings) noexcept
try {
	bool v = obs_data_get_bool(settings, ST_TAA);
	obs_property_set_visible(obs_properties_get(props, ST_TAA_RANGE_MINIMUM), v);
	obs_property_set_visible(obs_properties_get(props, ST_TAA_RANGE_MAXIMUM), v);
	obs_property_set_visible(obs_properties_get(props, ST_TAA_OFFSET_X), v);
	obs_property_set_visible(obs_properties_get(props, ST_TAA_OFFSET_Y), v);
	obs_property_set_visible(obs_properties_get(props, ST_TAA_COLOR), v);
	obs_property_set_visible(obs_properties_get(props, ST_TAA_ALPHA), v);
	return true;
} catch (const std::exception& ex) {
	DLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return true;
} catch (...) {
	DLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return true;
}

bool cb_modified_shadow_inside(void*, obs_properties_t* props, obs_property*, obs_data_t* settings) noexcept
try {
	bool v = obs_data_get_bool(settings, ST_SHADOW_INNER);
	obs_property_set_visible(obs_properties_get(props, ST_SHADOW_INNER_RANGE_MINIMUM), v);
	obs_property_set_visible(obs_properties_get(props, ST_SHADOW_INNER_RANGE_MAXIMUM), v);
	obs_property_set_visible(obs_properties_get(props, ST_SHADOW_INNER_OFFSET_X), v);
	obs_property_set_visible(obs_properties_get(props, ST_SHADOW_INNER_OFFSET_Y), v);
	obs_property_set_visible(obs_properties_get(props, ST_SHADOW_INNER_COLOR), v);
	obs_property_set_visible(obs_properties_get(props, ST_SHADOW_INNER_ALPHA), v);
	return true;
} catch (const std::exception& ex) {
	DLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return true;
} catch (...) {
	DLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return true;
}

bool cb_modified_shadow_outside(void*, obs_properties_t* props, obs_property*, obs_data_t* settings) noexcept
try {
	bool v = obs_data_get_bool(settings, ST_SHADOW_OUTER);
	obs_property_set_visible(obs_properties_get(props, ST_SHADOW_OUTER_RANGE_MINIMUM), v);
	obs_property_set_visible(obs_properties_get(props, ST_SHADOW_OUTER_RANGE_MAXIMUM), v);
	obs_property_set_visible(obs_properties_get(props, ST_SHADOW_OUTER_OFFSET_X), v);
	obs_property_set_visible(obs_properties_get(props, ST_SHADOW_OUTER_OFFSET_Y), v);
	obs_property_set_visible(obs_properties_get(props, ST_SHADOW_OUTER_COLOR), v);
	obs_property_set_visible(obs_properties_get(props, ST_SHADOW_OUTER_ALPHA), v);
	return true;
} catch (const std::exception& ex) {
	DLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return true;
} catch (...) {
	DLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return true;
}

bool cb_modified_glow_inside(void*, obs_properties_t* props, obs_property*, obs_data_t* settings) noexcept
try {
	bool v = obs_data_get_bool(settings, ST_GLOW_INNER);
	obs_property_set_visible(obs_properties_get(props, ST_GLOW_INNER_COLOR), v);
	obs_property_set_visible(obs_properties_get(props, ST_GLOW_INNER_ALPHA), v);
	obs_property_set_visible(obs_properties_get(props, ST_GLOW_INNER_WIDTH), v);
	obs_property_set_visible(obs_properties_get(props, ST_GLOW_INNER_SHARPNESS), v);
	return true;
} catch (const std::exception& ex) {
	DLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return true;
} catch (...) {
	DLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return true;
}

bool cb_modified_glow_outside(void*, obs_properties_t* props, obs_property*, obs_data_t* settings) noexcept
try {
	bool v = obs_data_get_bool(settings, ST_GLOW_OUTER);
	obs_property_set_visible(obs_properties_get(props, ST_GLOW_OUTER_COLOR), v);
	obs_property_set_visible(obs_properties_get(props, ST_GLOW_OUTER_ALPHA), v);
	obs_property_set_visible(obs_properties_get(props, ST_GLOW_OUTER_WIDTH), v);
	obs_property_set_visible(obs_properties_get(props, ST_GLOW_OUTER_SHARPNESS), v);
	return true;
} catch (const std::exception& ex) {
	DLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return true;
} catch (...) {
	DLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return true;
}

bool cb_modified_outline(void*, obs_properties_t* props, obs_property*, obs_data_t* settings) noexcept
try {
	bool v = obs_data_get_bool(settings, ST_OUTLINE);
	obs_property_set_visible(obs_properties_get(props, ST_OUTLINE_COLOR), v);
	obs_property_set_visible(obs_properties_get(props, ST_OUTLINE_ALPHA), v);
	obs_property_set_visible(obs_properties_get(props, ST_OUTLINE_WIDTH), v);
	obs_property_set_visible(obs_properties_get(props, ST_OUTLINE_OFFSET), v);
	obs_property_set_visible(obs_properties_get(props, ST_OUTLINE_SHARPNESS), v);
	return true;
} catch (const std::exception& ex) {
	DLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return true;
} catch (...) {
	DLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return true;
}

bool cb_modified_advanced(void*, obs_properties_t* props, obs_property*, obs_data_t* settings) noexcept
try {
	bool show_advanced = obs_data_get_bool(settings, S_ADVANCED);
	obs_property_set_visible(obs_properties_get(props, ST_SDF_SCALE), show_advanced);
	obs_property_set_visible(obs_properties_get(props, ST_SDF_THRESHOLD), show_advanced);
	return true;
} catch (const std::exception& ex) {
	DLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return true;
} catch (...) {
	DLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return true;
}

obs_properties_t* taa_factory::get_properties2(taa_instance* data)
{
	obs_properties_t* props = obs_properties_create();
	obs_property_t*   p     = nullptr;

	{
		p = obs_properties_add_bool(props, ST_TAA, D_TRANSLATE(ST_TAA));
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_TAA)));
		obs_property_set_modified_callback2(p, cb_modified_taa, data);
		p = obs_properties_add_float_slider(props, ST_TAA_RANGE_MINIMUM, D_TRANSLATE(ST_TAA_RANGE_MINIMUM), -16.0, 16.0,
											0.01);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_TAA_RANGE_MINIMUM)));
		p = obs_properties_add_float_slider(props, ST_TAA_RANGE_MAXIMUM, D_TRANSLATE(ST_TAA_RANGE_MAXIMUM), -16.0, 16.0,
											0.01);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_TAA_RANGE_MAXIMUM)));
		p = obs_properties_add_float_slider(props, ST_TAA_OFFSET_X, D_TRANSLATE(ST_TAA_OFFSET_X), -100.0, 100.0, 0.01);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_TAA_OFFSET_X)));
		p = obs_properties_add_float_slider(props, ST_TAA_OFFSET_Y, D_TRANSLATE(ST_TAA_OFFSET_Y), -100.0, 100.0, 0.01);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_TAA_OFFSET_Y)));
		p = obs_properties_add_color(props, ST_TAA_COLOR, D_TRANSLATE(ST_TAA_COLOR));
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_TAA_COLOR)));
		p = obs_properties_add_float_slider(props, ST_TAA_ALPHA, D_TRANSLATE(ST_TAA_ALPHA), 0.0, 100.0, 0.1);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_TAA_ALPHA)));
	}

	{
		p = obs_properties_add_bool(props, ST_SHADOW_OUTER, D_TRANSLATE(ST_SHADOW_OUTER));
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_OUTER)));
		obs_property_set_modified_callback2(p, cb_modified_shadow_outside, data);
		p = obs_properties_add_float_slider(props, ST_SHADOW_OUTER_RANGE_MINIMUM,
											D_TRANSLATE(ST_SHADOW_OUTER_RANGE_MINIMUM), -16.0, 16.0, 0.01);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_OUTER_RANGE_MINIMUM)));
		p = obs_properties_add_float_slider(props, ST_SHADOW_OUTER_RANGE_MAXIMUM,
											D_TRANSLATE(ST_SHADOW_OUTER_RANGE_MAXIMUM), -16.0, 16.0, 0.01);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_OUTER_RANGE_MAXIMUM)));
		p = obs_properties_add_float_slider(props, ST_SHADOW_OUTER_OFFSET_X, D_TRANSLATE(ST_SHADOW_OUTER_OFFSET_X),
											-100.0, 100.0, 0.01);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_OUTER_OFFSET_X)));
		p = obs_properties_add_float_slider(props, ST_SHADOW_OUTER_OFFSET_Y, D_TRANSLATE(ST_SHADOW_OUTER_OFFSET_Y),
											-100.0, 100.0, 0.01);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_OUTER_OFFSET_Y)));
		p = obs_properties_add_color(props, ST_SHADOW_OUTER_COLOR, D_TRANSLATE(ST_SHADOW_OUTER_COLOR));
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_OUTER_COLOR)));
		p = obs_properties_add_float_slider(props, ST_SHADOW_OUTER_ALPHA, D_TRANSLATE(ST_SHADOW_OUTER_ALPHA), 0.0,
											100.0, 0.1);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_OUTER_ALPHA)));
	}

	{
		p = obs_properties_add_bool(props, ST_SHADOW_INNER, D_TRANSLATE(ST_SHADOW_INNER));
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_INNER)));
		obs_property_set_modified_callback2(p, cb_modified_shadow_inside, data);
		p = obs_properties_add_float_slider(props, ST_SHADOW_INNER_RANGE_MINIMUM,
											D_TRANSLATE(ST_SHADOW_INNER_RANGE_MINIMUM), -16.0, 16.0, 0.01);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_INNER_RANGE_MINIMUM)));
		p = obs_properties_add_float_slider(props, ST_SHADOW_INNER_RANGE_MAXIMUM,
											D_TRANSLATE(ST_SHADOW_INNER_RANGE_MAXIMUM), -16.0, 16.0, 0.01);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_INNER_RANGE_MAXIMUM)));
		p = obs_properties_add_float_slider(props, ST_SHADOW_INNER_OFFSET_X, D_TRANSLATE(ST_SHADOW_INNER_OFFSET_X),
											-100.0, 100.0, 0.01);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_INNER_OFFSET_X)));
		p = obs_properties_add_float_slider(props, ST_SHADOW_INNER_OFFSET_Y, D_TRANSLATE(ST_SHADOW_INNER_OFFSET_Y),
											-100.0, 100.0, 0.01);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_INNER_OFFSET_Y)));
		p = obs_properties_add_color(props, ST_SHADOW_INNER_COLOR, D_TRANSLATE(ST_SHADOW_INNER_COLOR));
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_INNER_COLOR)));
		p = obs_properties_add_float_slider(props, ST_SHADOW_INNER_ALPHA, D_TRANSLATE(ST_SHADOW_INNER_ALPHA), 0.0,
											100.0, 0.1);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SHADOW_INNER_ALPHA)));
	}

	{
		p = obs_properties_add_bool(props, ST_GLOW_OUTER, D_TRANSLATE(ST_GLOW_OUTER));
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_GLOW_OUTER)));
		obs_property_set_modified_callback2(p, cb_modified_glow_outside, data);

		p = obs_properties_add_color(props, ST_GLOW_OUTER_COLOR, D_TRANSLATE(ST_GLOW_OUTER_COLOR));
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_GLOW_OUTER_COLOR)));
		p = obs_properties_add_float_slider(props, ST_GLOW_OUTER_ALPHA, D_TRANSLATE(ST_GLOW_OUTER_ALPHA), 0.0, 100.0,
											0.1);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_GLOW_OUTER_ALPHA)));

		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_GLOW_OUTER_WIDTH)));
		p = obs_properties_add_float_slider(props, ST_GLOW_OUTER_WIDTH, D_TRANSLATE(ST_GLOW_OUTER_WIDTH), 0.0, 16.0,
											0.01);

		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_GLOW_OUTER_SHARPNESS)));
		p = obs_properties_add_float_slider(props, ST_GLOW_OUTER_SHARPNESS, D_TRANSLATE(ST_GLOW_OUTER_SHARPNESS), 0.00,
											100.0, 0.01);
	}

	{
		p = obs_properties_add_bool(props, ST_GLOW_INNER, D_TRANSLATE(ST_GLOW_INNER));
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_GLOW_INNER)));
		obs_property_set_modified_callback2(p, cb_modified_glow_inside, data);

		p = obs_properties_add_color(props, ST_GLOW_INNER_COLOR, D_TRANSLATE(ST_GLOW_INNER_COLOR));
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_GLOW_INNER_COLOR)));
		p = obs_properties_add_float_slider(props, ST_GLOW_INNER_ALPHA, D_TRANSLATE(ST_GLOW_INNER_ALPHA), 0.0, 100.0,
											0.1);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_GLOW_INNER_ALPHA)));

		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_GLOW_INNER_WIDTH)));
		p = obs_properties_add_float_slider(props, ST_GLOW_INNER_WIDTH, D_TRANSLATE(ST_GLOW_INNER_WIDTH), 0.0, 16.0,
											0.01);

		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_GLOW_INNER_SHARPNESS)));
		p = obs_properties_add_float_slider(props, ST_GLOW_INNER_SHARPNESS, D_TRANSLATE(ST_GLOW_INNER_SHARPNESS), 0.00,
											100.0, 0.01);
	}

	{
		p = obs_properties_add_bool(props, ST_OUTLINE, D_TRANSLATE(ST_OUTLINE));
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_OUTLINE)));
		obs_property_set_modified_callback2(p, cb_modified_outline, data);
		p = obs_properties_add_color(props, ST_OUTLINE_COLOR, D_TRANSLATE(ST_OUTLINE_COLOR));
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_OUTLINE_COLOR)));
		p = obs_properties_add_float_slider(props, ST_OUTLINE_ALPHA, D_TRANSLATE(ST_OUTLINE_ALPHA), 0.0, 100.0, 0.1);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_OUTLINE_ALPHA)));

		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_OUTLINE_WIDTH)));
		p = obs_properties_add_float_slider(props, ST_OUTLINE_WIDTH, D_TRANSLATE(ST_OUTLINE_WIDTH), 0.0, 16.0, 0.01);

		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_OUTLINE_OFFSET)));
		p = obs_properties_add_float_slider(props, ST_OUTLINE_OFFSET, D_TRANSLATE(ST_OUTLINE_OFFSET), -16.0, 16.0,
											0.01);

		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_OUTLINE_SHARPNESS)));
		p = obs_properties_add_float_slider(props, ST_OUTLINE_SHARPNESS, D_TRANSLATE(ST_OUTLINE_SHARPNESS), 0.00, 100.0,
											0.01);
	}

	{
		p = obs_properties_add_bool(props, S_ADVANCED, D_TRANSLATE(S_ADVANCED));
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(S_ADVANCED)));
		obs_property_set_modified_callback2(p, cb_modified_advanced, data);

		p = obs_properties_add_float_slider(props, ST_SDF_SCALE, D_TRANSLATE(ST_SDF_SCALE), 0.1, 500.0, 0.1);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SDF_SCALE)));

		p = obs_properties_add_float_slider(props, ST_SDF_THRESHOLD, D_TRANSLATE(ST_SDF_THRESHOLD), 0.0, 100.0, 0.01);
		obs_property_set_long_description(p, D_TRANSLATE(D_DESC(ST_SDF_THRESHOLD)));
	}

	return props;
}

std::shared_ptr<taa_factory> _filter_taa_factory_instance = nullptr;

void streamfx::filter::taa::taa_factory::initialize()
{
	if (!_filter_taa_factory_instance)
		_filter_taa_factory_instance = std::make_shared<taa_factory>();
}

void streamfx::filter::taa::taa_factory::finalize()
{
	_filter_taa_factory_instance.reset();
}

std::shared_ptr<taa_factory> streamfx::filter::taa::taa_factory::get()
{
	return _filter_taa_factory_instance;
}
