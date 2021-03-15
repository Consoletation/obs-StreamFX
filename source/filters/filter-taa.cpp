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
#define ST_TAA "Filter.TAA"

using namespace streamfx::filter::taa;

taa_instance::taa_instance(obs_data_t* settings, obs_source_t* self)
	: obs::source_instance(settings, self), _source_rendered(false), _output_rendered(false), _taa(false)
{
	{
		auto gctx        = gs::context();
		vec4 transparent = {0, 0, 0, 0};

		DLOG_INFO("Initializing buffers");
		this->_source_rt = std::make_shared<gs::rendertarget>(GS_RGBA, GS_ZS_NONE);
		this->_taa_write = std::make_shared<gs::rendertarget>(GS_RGBA32F, GS_ZS_NONE);
		this->_taa_read  = std::make_shared<gs::rendertarget>(GS_RGBA32F, GS_ZS_NONE);
		this->_output_rt = std::make_shared<gs::rendertarget>(GS_RGBA, GS_ZS_NONE);

		std::shared_ptr<gs::rendertarget> initialize_rts[] = {_source_rt, _taa_write, _taa_read, _output_rt};
		// std::shared_ptr<gs::rendertarget> initialize_rts[] = {_source_rt, _output_rt};
		for (auto rt : initialize_rts) {
			auto op = rt->render(1, 1);
			gs_clear(GS_CLEAR_COLOR | GS_CLEAR_DEPTH, &transparent, 0, 0);
			DLOG_INFO("buffers.");
		}

		// Load Effects
		{
			char* file = obs_module_file("effects/taa.effect");
			if (!file) {
				DLOG_ERROR(LOG_PREFIX "Unable to load _taa_effect as file is missing or locked.");
			}
			try {
				_taa_effect = gs::effect::create(file);
			} catch (std::runtime_error& ex) {
				DLOG_ERROR("Loading _effect '%s' failed with error(s): %s", file, ex.what());
			}
			DLOG_INFO("Loaded effect file");
			bfree(file);
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
		_taa = obs_data_get_bool(data, ST_TAA);
	}
}

void taa_instance::video_tick(float_t)
{
	if (obs_source_t* target = obs_filter_get_target(_self); target != nullptr) {
		_source_rendered = false;
		_output_rendered = false;
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

				auto op = this->_source_rt->render(baseW, baseH);
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

				if (!_taa_effect) {
					throw std::runtime_error("SDF Effect no loaded");
				}

				{
#ifdef ENABLE_PROFILING
					gs::debug_marker gdm{gs::debug_color_convert, "Update Distance Field"};
#endif

					auto op = _taa_write->render(uint32_t(baseW), uint32_t(baseH));
					gs_ortho(0, 1, 0, 1, -1, 1);
					gs_clear(GS_CLEAR_COLOR | GS_CLEAR_DEPTH, &color_transparent, 0, 0);

					_taa_effect.get_parameter("_image").set_texture(_source_texture);
					_taa_effect.get_parameter("_size").set_float2(float_t(baseW), float_t(baseH));
					_taa_effect.get_parameter("_taa").set_texture(_taa_texture);
					_taa_effect.get_parameter("_threshold").set_float(0.5);

					while (gs_effect_loop(_taa_effect.get_object(), "Draw")) {
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

		if (!_taa_effect) {
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
				_taa_effect.get_parameter("pSDFTexture").set_texture(_taa_texture);
				//_taa_effect.get_parameter("pSDFTexture").set_texture(_source_texture);
				_taa_effect.get_parameter("pSDFThreshold").set_float(0.5);
				_taa_effect.get_parameter("pImageTexture").set_texture(_source_texture->get_object());
				_taa_effect.get_parameter("pTAAColor").set_float4(0.0, 0.0, 0.0, 0.0);
				_taa_effect.get_parameter("pTAAMin").set_float(0.0);
				_taa_effect.get_parameter("pTAAMax").set_float(4.0);
				_taa_effect.get_parameter("pTAAOffset").set_float2(0.0 / float_t(baseW), 0.0 / float_t(baseH));
				while (gs_effect_loop(_taa_effect.get_object(), "TAA")) {
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
}

bool cb_modified_taa(void*, obs_properties_t* props, obs_property*, obs_data_t* settings) noexcept
try {
	bool v = obs_data_get_bool(settings, ST_TAA);
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
