// FFMPEG Video Encoder Integration for OBS Studio
// Copyright (C) 2018 - 2018 Michael Fabian Dirks
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

#ifndef OBS_FFMPEG_ENCODER_HPP
#define OBS_FFMPEG_ENCODER_HPP
#pragma once

#include <map>
#include <string>
#include "ffmpeg/swscale.hpp"

#include <obs.h>

extern "C" {
#pragma warning(push)
#pragma warning(disable : 4244)
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#pragma warning(pop)
}

namespace obsffmpeg {
	namespace encoder {
		class base {
			protected:
			obs_encoder_t* self = nullptr;

			AVCodec*        avcodec = nullptr;
			AVCodecContext* avcontext = nullptr;
			AVDictionary*   avdictionary = nullptr;
			ffmpeg::swscale swscale;

			std::pair<uint32_t, uint32_t> resolution;
			std::pair<uint32_t, uint32_t> framerate;

			AVPixelFormat source_format;
			AVColorSpace  source_colorspace;
			AVColorRange  source_range;

			AVPixelFormat target_format;
			AVColorSpace  target_colorspace;
			AVColorRange  target_range;

			public:
			virtual ~base();

			virtual void get_properties(obs_properties_t* props) = 0;

			virtual bool update(obs_data_t* settings) = 0;

			virtual bool get_extra_data(uint8_t** extra_data, size_t* size) = 0;

			virtual bool get_sei_data(uint8_t** sei_data, size_t* size) = 0;

			virtual void get_video_info(struct video_scale_info* info) = 0;

			virtual bool encode(struct encoder_frame* frame, struct encoder_packet* packet,
			                    bool* received_packet) = 0;
		};

#define make_encoder_base(_source, _target, _name, _codec) \
	static void* _source##_create(obs_data_t* settings, obs_encoder_t* encoder) \
	{ \
		_target* ptr = nullptr; \
		try { \
			ptr = new _target(settings, encoder); \
		} catch (...) { \
		} \
		return ptr; \
	} \
	static void _source##_destroy(void* ptr) \
	{ \
		delete static_cast<_target*>(ptr); \
	} \
	static const char* _source##_get_name(void*) \
	{ \
		return _name; \
	} \
	static obs_properties_t* _source##_get_properties(void* ptr) \
	{ \
		obs_properties_t* pr = _target::get_properties(); \
		if (ptr) { \
			static_cast<_target*>(ptr)->get_properties(pr); \
		} \
		return pr; \
	} \
	static void _source##_get_defaults(obs_data_t* settings) \
	{ \
		_target::get_defaults(settings); \
	} \
	static bool _source##_get_extra_data(void* ptr, uint8_t** extra_data, size_t* size) \
	{ \
		return static_cast<_target*>(ptr)->get_extra_data(extra_data, size); \
	} \
	static bool _source##_get_sei_data(void* ptr, uint8_t** sei_data, size_t* size) \
	{ \
		return static_cast<_target*>(ptr)->get_sei_data(sei_data, size); \
	} \
	static void _source##_get_video_info(void* ptr, struct video_scale_info* info) \
	{ \
		static_cast<_target*>(ptr)->get_video_info(info); \
	} \
	static bool _source##_encode(void* ptr, struct encoder_frame* frame, struct encoder_packet* packet, \
	                             bool* received_packet) \
	{ \
		return static_cast<_target*>(ptr)->encode(frame, packet, received_packet); \
	} \
	static obs_encoder_info _source##_info; \
	static void             _source##_initialize() \
	{ \
		_source##_info.id             = "obs-ffmpeg-encoder-" #_source; \
		_source##_info.type           = OBS_ENCODER_VIDEO; \
		_source##_info.caps           = 0; \
		_source##_info.codec          = _codec; \
		_source##_info.create         = _source##_create; \
		_source##_info.destroy        = _source##_destroy; \
		_source##_info.get_name       = _source##_get_name; \
		_source##_info.get_properties = _source##_get_properties; \
		_source##_info.get_defaults   = _source##_get_defaults; \
		_source##_info.get_extra_data = _source##_get_extra_data; \
		_source##_info.get_sei_data   = _source##_get_sei_data; \
		_source##_info.get_video_info = _source##_get_video_info; \
		_source##_info.encode         = _source##_encode; \
		obs_register_encoder(&(_source##_info)); \
	}
		//*/

	} // namespace encoder
} // namespace obsffmpeg

#endif OBS_FFMPEG_ENCODER_HPP