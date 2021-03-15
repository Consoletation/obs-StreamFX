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

#pragma once
#include "common.hpp"
#include "obs/gs/gs-effect.hpp"
#include "obs/gs/gs-rendertarget.hpp"
#include "obs/gs/gs-sampler.hpp"
#include "obs/gs/gs-texture.hpp"
#include "obs/gs/gs-vertexbuffer.hpp"
#include "obs/obs-source-factory.hpp"

namespace streamfx::filter::taa {
	class taa_instance : public obs::source_instance {
		gs::effect _taa_effect;

		// Input
		std::shared_ptr<gs::rendertarget> _source_rt;
		std::shared_ptr<gs::texture>      _source_texture;
		bool                              _source_rendered;

		// Effects
		std::shared_ptr<gs::texture>      _output_texture;
		std::shared_ptr<gs::rendertarget> _output_rt;
		bool                              _output_rendered;

		// // Distance Field
		std::shared_ptr<gs::rendertarget> _taa_write;
		std::shared_ptr<gs::rendertarget> _taa_read;
		std::shared_ptr<gs::texture>      _taa_texture;

		/// Temporal Anti-Aliasing
		bool _taa;

		public:
		taa_instance(obs_data_t* settings, obs_source_t* self);
		virtual ~taa_instance();

		virtual void load(obs_data_t* settings) override;
		virtual void migrate(obs_data_t* data, uint64_t version) override;
		virtual void update(obs_data_t* settings) override;

		virtual void video_tick(float_t) override;
		virtual void video_render(gs_effect_t*) override;
	};

	class taa_factory : public obs::source_factory<filter::taa::taa_factory, filter::taa::taa_instance> {
		public:
		taa_factory();
		virtual ~taa_factory();

		virtual const char* get_name() override;

		virtual void get_defaults2(obs_data_t* data) override;

		virtual obs_properties_t* get_properties2(filter::taa::taa_instance* data) override;

		public: // Singleton
		static void initialize();

		static void finalize();

		static std::shared_ptr<taa_factory> get();
	};

} // namespace streamfx::filter::taa
