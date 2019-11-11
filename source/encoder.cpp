// FFMPEG Video Encoder Integration for OBS Studio
// Copyright (c) 2019 Michael Fabian Dirks <info@xaymar.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "encoder.hpp"
#include <iomanip>
#include <set>
#include <sstream>
#include <stack>
#include <thread>
#include <util/profiler.hpp>
#include <vector>
#include "codecs/hevc.hpp"
#include "ffmpeg/tools.hpp"
#include "plugin.hpp"
#include "strings.hpp"
#include "utility.hpp"

extern "C" {
#include <obs-avc.h>
#include <obs-module.h>
#pragma warning(push)
#pragma warning(disable : 4244)
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#pragma warning(pop)
}

//#define DEBUG_CALL_ORDER
// Call Order should be:
// - create_texture/create
// - get_video_info
// - encode_texture/encode
// I don't understand what get_video_info is actually for in this order, as this postpones initialization to encode...

#ifdef WIN32
#define HARDWARE_ENCODING
#include "hwapi/d3d11.hpp"
#endif

// FFmpeg
#define ST_FFMPEG "FFmpeg"
#define ST_FFMPEG_CUSTOMSETTINGS "FFmpeg.CustomSettings"
#define ST_FFMPEG_THREADS "FFmpeg.Threads"
#define ST_FFMPEG_COLORFORMAT "FFmpeg.ColorFormat"
#define ST_FFMPEG_STANDARDCOMPLIANCE "FFmpeg.StandardCompliance"
#define ST_FFMPEG_GPU "FFmpeg.GPU"

enum class keyframe_type { SECONDS, FRAMES };

static void* _create(obs_data_t* settings, obs_encoder_t* encoder) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX %llX", __FUNCTION_NAME__, settings, encoder);
#endif
	return reinterpret_cast<void*>(new obsffmpeg::encoder(settings, encoder));
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return nullptr;
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return nullptr;
}

static void* _create_texture(obs_data_t* settings, obs_encoder_t* encoder) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX %llX", __FUNCTION_NAME__, settings, encoder);
#endif
	return reinterpret_cast<void*>(new obsffmpeg::encoder(settings, encoder, true));
} catch (const obsffmpeg::unsupported_gpu_exception&) {
	obsffmpeg::encoder_factory* fac =
	    reinterpret_cast<obsffmpeg::encoder_factory*>(obs_encoder_get_type_data(encoder));
	PLOG_WARNING("<%s> GPU not supported for hardware encoding, falling back to software.",
	             fac->get_avcodec()->name);
	return obs_encoder_create_rerouted(encoder, fac->get_fallback().oei.id);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return nullptr;
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return nullptr;
}

static void _destroy(void* ptr) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX", __FUNCTION_NAME__, ptr);
#endif
	if (ptr)
		delete reinterpret_cast<obsffmpeg::encoder*>(ptr);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
}

static const char* _get_name(void* type_data) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX", __FUNCTION_NAME__, type_data);
#endif
	return reinterpret_cast<obsffmpeg::encoder_factory*>(type_data)->get_info().readable_name.c_str();
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return nullptr;
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return nullptr;
}

static const char* _get_name_fallback(void* type_data) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX", __FUNCTION_NAME__, type_data);
#endif
	return reinterpret_cast<obsffmpeg::encoder_factory*>(type_data)->get_fallback().readable_name.c_str();
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return nullptr;
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return nullptr;
}

static void _get_defaults(obs_data_t* settings, void* type_data) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX %llX", __FUNCTION_NAME__, settings, type_data);
#endif
	reinterpret_cast<obsffmpeg::encoder_factory*>(type_data)->get_defaults(settings);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
};

static void _get_defaults_texture(obs_data_t* settings, void* type_data) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX %llX", __FUNCTION_NAME__, settings, type_data);
#endif
	reinterpret_cast<obsffmpeg::encoder_factory*>(type_data)->get_defaults(settings, true);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
};

static obs_properties_t* _get_properties(void* ptr, void* type_data) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX %llX", __FUNCTION_NAME__, ptr, type_data);
#endif
	obs_properties_t* props = obs_properties_create();
	if (type_data != nullptr) {
		reinterpret_cast<obsffmpeg::encoder_factory*>(type_data)->get_properties(props);
	}
	if (ptr != nullptr) {
		reinterpret_cast<obsffmpeg::encoder*>(ptr)->get_properties(props);
	}
	return props;
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return reinterpret_cast<obs_properties_t*>(0);
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return reinterpret_cast<obs_properties_t*>(0);
}

static obs_properties_t* _get_properties_texture(void* ptr, void* type_data) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX %llX", __FUNCTION_NAME__, ptr, type_data);
#endif
	obs_properties_t* props = obs_properties_create();
	if (type_data != nullptr) {
		reinterpret_cast<obsffmpeg::encoder_factory*>(type_data)->get_properties(props, true);
	}
	if (ptr != nullptr) {
		reinterpret_cast<obsffmpeg::encoder*>(ptr)->get_properties(props, true);
	}
	return props;
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return reinterpret_cast<obs_properties_t*>(0);
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return reinterpret_cast<obs_properties_t*>(0);
}

static bool _update(void* ptr, obs_data_t* settings) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX %llX", __FUNCTION_NAME__, ptr, settings);
#endif
	return reinterpret_cast<obsffmpeg::encoder*>(ptr)->update(settings);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return false;
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return false;
}

static bool _get_sei_data(void* ptr, uint8_t** sei_data, size_t* size) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX %llX %llX", __FUNCTION_NAME__, ptr, sei_data, size);
#endif
	return reinterpret_cast<obsffmpeg::encoder*>(ptr)->get_sei_data(sei_data, size);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return false;
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return false;
}

static bool _get_extra_data(void* ptr, uint8_t** extra_data, size_t* size) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX %llX %llX", __FUNCTION_NAME__, ptr, extra_data, size);
#endif
	return reinterpret_cast<obsffmpeg::encoder*>(ptr)->get_extra_data(extra_data, size);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return false;
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return false;
}

static void _get_video_info(void* ptr, struct video_scale_info* info) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX %llX", __FUNCTION_NAME__, ptr, info);
#endif
	reinterpret_cast<obsffmpeg::encoder*>(ptr)->get_video_info(info);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
}

static bool _encode(void* ptr, struct encoder_frame* frame, struct encoder_packet* packet,
                    bool* received_packet) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX %llX %llX %llX", __FUNCTION_NAME__, ptr, frame, packet, received_packet);
#endif
	return reinterpret_cast<obsffmpeg::encoder*>(ptr)->video_encode(frame, packet, received_packet);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return false;
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return false;
}

static bool _encode_texture(void* ptr, uint32_t handle, int64_t pts, uint64_t lock_key, uint64_t* next_key,
                            struct encoder_packet* packet, bool* received_packet) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %lI %llI %llU %llX %llX %llX", __FUNCTION_NAME__, ptr, handle, pts, lock_key, next_key, packet,
	          received_packet);
#endif
	return reinterpret_cast<obsffmpeg::encoder*>(ptr)->video_encode_texture(handle, pts, lock_key, next_key, packet,
	                                                                        received_packet);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return false;
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return false;
}

static void _get_audio_info(void* ptr, struct audio_convert_info* info) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX %llX", __FUNCTION_NAME__, ptr, info);
#endif
	reinterpret_cast<obsffmpeg::encoder*>(ptr)->get_audio_info(info);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
}

static size_t _get_frame_size(void* ptr) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX", __FUNCTION_NAME__, ptr);
#endif
	return reinterpret_cast<obsffmpeg::encoder*>(ptr)->get_frame_size();
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return 0;
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return 0;
}

static bool _encode_audio(void* ptr, struct encoder_frame* frame, struct encoder_packet* packet,
                          bool* received_packet) noexcept
try {
#ifdef DEBUG_CALL_ORDER
	PLOG_INFO("%s %llX %llX %llX %llX", __FUNCTION_NAME__, ptr, frame, packet, received_packet);
#endif
	return reinterpret_cast<obsffmpeg::encoder*>(ptr)->audio_encode(frame, packet, received_packet);
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return false;
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return false;
}

obsffmpeg::encoder_factory::encoder_factory(const AVCodec* codec) : avcodec_ptr(codec), info(), info_fallback()
{
	// Find Codec UI handler.
	_handler = obsffmpeg::find_codec_handler(avcodec_ptr->name);

	// Unique Id is FFmpeg name.
	info.uid = std::string("obs-ffmpeg-encoder_") + avcodec_ptr->name;

	// Also generate a human readable name while we're at it.
	{
		std::stringstream sstr;
		if (!obsffmpeg::has_codec_handler(avcodec_ptr->name)) {
			sstr << "[UNSUPPORTED] ";
		}
		sstr << (avcodec_ptr->long_name ? avcodec_ptr->long_name : avcodec_ptr->name);
		if (avcodec_ptr->long_name) {
			sstr << " (" << avcodec_ptr->name << ")";
		}
		info.readable_name = sstr.str();

		// Allow UI Handler to replace visible name.
		if (_handler)
			_handler->override_visible_name(avcodec_ptr, info.readable_name);
	}

	// Assign Ids.
	{
		const AVCodecDescriptor* desc = avcodec_descriptor_get(avcodec_ptr->id);
		if (desc) {
			info.codec = desc->name;
		} else {
			// Fall back to encoder name in the case that FFmpeg itself doesn't know
			// what codec this actually is.
			info.codec = avcodec_ptr->name;
		}
	}

	info.oei.id    = info.uid.c_str();
	info.oei.codec = info.codec.c_str();

#ifndef _DEBUG
	// Is this a deprecated encoder?
	if (!obsffmpeg::has_codec_handler(avcodec_ptr->name)) {
		info.oei.caps |= OBS_ENCODER_CAP_DEPRECATED;
	}
#endif

	// Hardware encoder?
#ifdef HARDWARE_ENCODING
	if (ffmpeg::tools::can_hardware_encode(avcodec_ptr)) {
		info_fallback.uid           = info.uid + "_sw";
		info_fallback.codec         = info.codec;
		info_fallback.readable_name = info.readable_name + " (Software)";

		// Copy capabilities and hide from view.
		info_fallback.oei.id    = info_fallback.uid.c_str();
		info_fallback.oei.codec = info.oei.codec;
		info_fallback.oei.caps  = info.oei.caps;
		//info_fallback.oei.caps |= OBS_ENCODER_CAP_DEPRECATED;

		info.oei.caps |= OBS_ENCODER_CAP_PASS_TEXTURE;
	}
#endif
}

obsffmpeg::encoder_factory::~encoder_factory() {}

void obsffmpeg::encoder_factory::register_encoder()
{
	// Detect encoder type (only Video and Audio supported)
	if (avcodec_ptr->type == AVMediaType::AVMEDIA_TYPE_VIDEO) {
		info.oei.type = obs_encoder_type::OBS_ENCODER_VIDEO;
	} else if (avcodec_ptr->type == AVMediaType::AVMEDIA_TYPE_AUDIO) {
		info.oei.type = obs_encoder_type::OBS_ENCODER_AUDIO;
	} else {
		throw std::invalid_argument("unsupported codec type");
	}

	// Register functions.
	info.oei.destroy         = _destroy;
	info.oei.get_name        = _get_name;
	info.oei.get_defaults2   = _get_defaults;
	info.oei.get_properties2 = _get_properties;
	info.oei.update          = _update;
	info.oei.get_sei_data    = _get_sei_data;
	info.oei.get_extra_data  = _get_extra_data;

	if (avcodec_ptr->type == AVMediaType::AVMEDIA_TYPE_VIDEO) {
		info.oei.get_video_info = _get_video_info;
	} else if (avcodec_ptr->type == AVMediaType::AVMEDIA_TYPE_AUDIO) {
		info.oei.get_audio_info = _get_audio_info;
		info.oei.get_frame_size = _get_frame_size;
		info.oei.encode         = _encode_audio;
	}

	// Finally store ourself as type data.
	info.oei.type_data = this;

	if (ffmpeg::tools::can_hardware_encode(avcodec_ptr)) {
		info.oei.create          = _create_texture;
		info.oei.encode_texture  = _encode_texture;
		info.oei.get_defaults2   = _get_defaults_texture;
		info.oei.get_properties2 = _get_properties_texture;

		info_fallback.oei.type            = info.oei.type;
		info_fallback.oei.create          = _create;
		info_fallback.oei.destroy         = _destroy;
		info_fallback.oei.get_name        = _get_name_fallback;
		info_fallback.oei.get_defaults2   = _get_defaults;
		info_fallback.oei.get_properties2 = _get_properties;
		info_fallback.oei.update          = _update;
		info_fallback.oei.get_sei_data    = _get_sei_data;
		info_fallback.oei.get_extra_data  = _get_extra_data;
		info_fallback.oei.get_video_info  = _get_video_info;
		info_fallback.oei.encode          = _encode;
		info_fallback.oei.type_data       = this;

		obs_register_encoder(&info_fallback.oei);
		PLOG_DEBUG("Registered software fallback for encoder #%llX", avcodec_ptr);
	} else {
		// Is not a GPU Encoder, don't implement fallback.
		info.oei.create = _create;
		info.oei.encode = _encode;
	}

	obs_register_encoder(&info.oei);
	PLOG_DEBUG("Registered encoder #%llX with name '%s' and long name '%s' and caps %llX", avcodec_ptr,
	           avcodec_ptr->name, avcodec_ptr->long_name, avcodec_ptr->capabilities);
}

void obsffmpeg::encoder_factory::get_defaults(obs_data_t* settings, bool hw_encode)
{
	if (_handler)
		_handler->get_defaults(settings, avcodec_ptr, nullptr, hw_encode);

	if ((avcodec_ptr->capabilities & AV_CODEC_CAP_INTRA_ONLY) == 0) {
		obs_data_set_default_int(settings, S_KEYFRAMES_INTERVALTYPE, 0);
		obs_data_set_default_double(settings, S_KEYFRAMES_INTERVAL_SECONDS, 2.0);
		obs_data_set_default_int(settings, S_KEYFRAMES_INTERVAL_FRAMES, 300);
	}

	{ // Integrated Options
		// FFmpeg
		obs_data_set_default_string(settings, ST_FFMPEG_CUSTOMSETTINGS, "");
		if (!hw_encode) {
			obs_data_set_default_int(settings, ST_FFMPEG_COLORFORMAT,
			                         static_cast<int64_t>(AV_PIX_FMT_NONE));
			obs_data_set_default_int(settings, ST_FFMPEG_THREADS, 0);
			obs_data_set_default_int(settings, ST_FFMPEG_GPU, 0);
		}
		obs_data_set_default_int(settings, ST_FFMPEG_STANDARDCOMPLIANCE, FF_COMPLIANCE_STRICT);
	}
}

static bool modified_keyframes(obs_properties_t* props, obs_property_t*, obs_data_t* settings)
try {
	bool is_seconds = obs_data_get_int(settings, S_KEYFRAMES_INTERVALTYPE) == 0;
	obs_property_set_visible(obs_properties_get(props, S_KEYFRAMES_INTERVAL_FRAMES), !is_seconds);
	obs_property_set_visible(obs_properties_get(props, S_KEYFRAMES_INTERVAL_SECONDS), is_seconds);
	return true;
} catch (const std::exception& ex) {
	PLOG_ERROR("Unexpected exception in function '%s': %s.", __FUNCTION_NAME__, ex.what());
	return false;
} catch (...) {
	PLOG_ERROR("Unexpected exception in function '%s'.", __FUNCTION_NAME__);
	return false;
}

void obsffmpeg::encoder_factory::get_properties(obs_properties_t* props, bool hw_encode)
{
	if (_handler)
		_handler->get_properties(props, avcodec_ptr, nullptr, hw_encode);

	if ((avcodec_ptr->capabilities & AV_CODEC_CAP_INTRA_ONLY) == 0) {
		// Key-Frame Options
		obs_properties_t* grp = props;
		if (!obsffmpeg::are_property_groups_broken()) {
			grp = obs_properties_create();
			obs_properties_add_group(props, S_KEYFRAMES, TRANSLATE(S_KEYFRAMES), OBS_GROUP_NORMAL, grp);
		}

		{
			auto p =
			    obs_properties_add_list(grp, S_KEYFRAMES_INTERVALTYPE, TRANSLATE(S_KEYFRAMES_INTERVALTYPE),
			                            OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
			obs_property_set_long_description(p, TRANSLATE(DESC(S_KEYFRAMES_INTERVALTYPE)));
			obs_property_set_modified_callback(p, modified_keyframes);
			obs_property_list_add_int(p, TRANSLATE(S_KEYFRAMES_INTERVALTYPE_(Seconds)), 0);
			obs_property_list_add_int(p, TRANSLATE(S_KEYFRAMES_INTERVALTYPE_(Frames)), 1);
		}
		{
			auto p =
			    obs_properties_add_float(grp, S_KEYFRAMES_INTERVAL_SECONDS, TRANSLATE(S_KEYFRAMES_INTERVAL),
			                             0.00, std::numeric_limits<int16_t>::max(), 0.01);
			obs_property_set_long_description(p, TRANSLATE(DESC(S_KEYFRAMES_INTERVAL)));
			obs_property_float_set_suffix(p, " seconds");
		}
		{
			auto p =
			    obs_properties_add_int(grp, S_KEYFRAMES_INTERVAL_FRAMES, TRANSLATE(S_KEYFRAMES_INTERVAL), 0,
			                           std::numeric_limits<int32_t>::max(), 1);
			obs_property_set_long_description(p, TRANSLATE(DESC(S_KEYFRAMES_INTERVAL)));
			obs_property_int_set_suffix(p, " frames");
		}
	}

	{
		obs_properties_t* grp = props;
		if (!obsffmpeg::are_property_groups_broken()) {
			auto prs = obs_properties_create();
			obs_properties_add_group(props, ST_FFMPEG, TRANSLATE(ST_FFMPEG), OBS_GROUP_NORMAL, prs);
			grp = prs;
		}

		{
			auto p =
			    obs_properties_add_text(grp, ST_FFMPEG_CUSTOMSETTINGS, TRANSLATE(ST_FFMPEG_CUSTOMSETTINGS),
			                            obs_text_type::OBS_TEXT_DEFAULT);
			obs_property_set_long_description(p, TRANSLATE(DESC(ST_FFMPEG_CUSTOMSETTINGS)));
		}
		if (!hw_encode) {
      {
        auto p = obs_properties_add_int(grp, ST_FFMPEG_GPU, TRANSLATE(ST_FFMPEG_GPU), 0, std::numeric_limits<uint8_t>::max(), 1);
        obs_property_set_long_description(p, TRANSLATE(DESC(ST_FFMPEG_GPU)));
      }
			if (avcodec_ptr->pix_fmts) {
				auto p = obs_properties_add_list(grp, ST_FFMPEG_COLORFORMAT,
				                                 TRANSLATE(ST_FFMPEG_COLORFORMAT), OBS_COMBO_TYPE_LIST,
				                                 OBS_COMBO_FORMAT_INT);
				obs_property_set_long_description(p, TRANSLATE(DESC(ST_FFMPEG_COLORFORMAT)));
				obs_property_list_add_int(p, TRANSLATE(S_STATE_AUTOMATIC),
				                          static_cast<int64_t>(AV_PIX_FMT_NONE));
				for (auto ptr = avcodec_ptr->pix_fmts; *ptr != AV_PIX_FMT_NONE; ptr++) {
					obs_property_list_add_int(p, ffmpeg::tools::get_pixel_format_name(*ptr),
					                          static_cast<int64_t>(*ptr));
				}
			}
			if (avcodec_ptr->capabilities & (AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_SLICE_THREADS)) {
				auto p =
				    obs_properties_add_int_slider(grp, ST_FFMPEG_THREADS, TRANSLATE(ST_FFMPEG_THREADS),
				                                  0, std::thread::hardware_concurrency() * 2, 1);
				obs_property_set_long_description(p, TRANSLATE(DESC(ST_FFMPEG_THREADS)));
			}
		}
		{
			auto p = obs_properties_add_list(grp, ST_FFMPEG_STANDARDCOMPLIANCE,
			                                 TRANSLATE(ST_FFMPEG_STANDARDCOMPLIANCE), OBS_COMBO_TYPE_LIST,
			                                 OBS_COMBO_FORMAT_INT);
			obs_property_set_long_description(p, TRANSLATE(DESC(ST_FFMPEG_STANDARDCOMPLIANCE)));
			obs_property_list_add_int(p, TRANSLATE(ST_FFMPEG_STANDARDCOMPLIANCE ".VeryStrict"),
			                          FF_COMPLIANCE_VERY_STRICT);
			obs_property_list_add_int(p, TRANSLATE(ST_FFMPEG_STANDARDCOMPLIANCE ".Strict"),
			                          FF_COMPLIANCE_STRICT);
			obs_property_list_add_int(p, TRANSLATE(ST_FFMPEG_STANDARDCOMPLIANCE ".Normal"),
			                          FF_COMPLIANCE_NORMAL);
			obs_property_list_add_int(p, TRANSLATE(ST_FFMPEG_STANDARDCOMPLIANCE ".Unofficial"),
			                          FF_COMPLIANCE_UNOFFICIAL);
			obs_property_list_add_int(p, TRANSLATE(ST_FFMPEG_STANDARDCOMPLIANCE ".Experimental"),
			                          FF_COMPLIANCE_EXPERIMENTAL);
		}
	};
}

const AVCodec* obsffmpeg::encoder_factory::get_avcodec()
{
	return avcodec_ptr;
}

const obsffmpeg::encoder_info& obsffmpeg::encoder_factory::get_info()
{
	return info;
}

const obsffmpeg::encoder_info& obsffmpeg::encoder_factory::get_fallback()
{
	return info_fallback;
}

void obsffmpeg::encoder::initialize_sw(obs_data_t* settings)
{
	if (_codec->type == AVMEDIA_TYPE_VIDEO) {
		// Initialize Video Encoding
		auto voi = video_output_get_info(obs_encoder_video(_self));

		// Find a suitable Pixel Format.
		AVPixelFormat _pixfmt_source = ffmpeg::tools::obs_videoformat_to_avpixelformat(voi->format);
		AVPixelFormat _pixfmt_target =
		    static_cast<AVPixelFormat>(obs_data_get_int(settings, ST_FFMPEG_COLORFORMAT));
		if (_pixfmt_target == AV_PIX_FMT_NONE) {
			// Find the best conversion format.
			_pixfmt_target = ffmpeg::tools::get_least_lossy_format(_codec->pix_fmts, _pixfmt_source);

			if (_handler) // Allow Handler to override the automatic color format for sanity reasons.
				_handler->override_colorformat(_pixfmt_target, settings, _codec, _context);
		} else {
			// Use user override, guaranteed to be supported.
			bool is_format_supported = false;
			for (auto ptr = _codec->pix_fmts; *ptr != AV_PIX_FMT_NONE; ptr++) {
				if (*ptr == _pixfmt_target) {
					is_format_supported = true;
				}
			}

			if (!is_format_supported) {
				std::stringstream sstr;
				sstr << "Color Format '" << ffmpeg::tools::get_pixel_format_name(_pixfmt_target)
				     << "' is not supported by the encoder.";
				throw std::exception(sstr.str().c_str());
			}
		}

		_context->width  = static_cast<int>(obs_encoder_get_width(_self));
		_context->height = static_cast<int>(obs_encoder_get_height(_self));
		ffmpeg::tools::setup_obs_color(voi->colorspace, voi->range, _context);

		_context->pix_fmt                 = _pixfmt_target;
		_context->field_order             = AV_FIELD_PROGRESSIVE;
		_context->ticks_per_frame         = 1;
		_context->sample_aspect_ratio.num = _context->sample_aspect_ratio.den = 1;
		_context->framerate.num = _context->time_base.den = voi->fps_num;
		_context->framerate.den = _context->time_base.num = voi->fps_den;

		_swscale.set_source_size(_context->width, _context->height);
		_swscale.set_source_color(_context->color_range == AVCOL_RANGE_JPEG, _context->colorspace);
		_swscale.set_source_format(_pixfmt_source);

		_swscale.set_target_size(_context->width, _context->height);
		_swscale.set_target_color(_context->color_range == AVCOL_RANGE_JPEG, _context->colorspace);
		_swscale.set_target_format(_pixfmt_target);

		// Create Scaler
		if (!_swscale.initialize(SWS_POINT)) {
			std::stringstream sstr;
			sstr << "Initializing scaler failed for conversion from '"
			     << ffmpeg::tools::get_pixel_format_name(_swscale.get_source_format()) << "' to '"
			     << ffmpeg::tools::get_pixel_format_name(_swscale.get_target_format())
			     << "' with color space '"
			     << ffmpeg::tools::get_color_space_name(_swscale.get_source_colorspace()) << "' and "
			     << (_swscale.is_source_full_range() ? "full" : "partial") << " range.";
			throw std::runtime_error(sstr.str());
		}
	}
}

void obsffmpeg::encoder::initialize_hw(obs_data_t*)
{
	// Initialize Video Encoding
	auto voi = video_output_get_info(obs_encoder_video(_self));

	_context->width                   = voi->width;
	_context->height                  = voi->height;
	_context->field_order             = AV_FIELD_PROGRESSIVE;
	_context->ticks_per_frame         = 1;
	_context->sample_aspect_ratio.num = _context->sample_aspect_ratio.den = 1;
	_context->framerate.num = _context->time_base.den = voi->fps_num;
	_context->framerate.den = _context->time_base.num = voi->fps_den;
	ffmpeg::tools::setup_obs_color(voi->colorspace, voi->range, _context);
	_context->sw_pix_fmt = ffmpeg::tools::obs_videoformat_to_avpixelformat(voi->format);

#ifdef WIN32
	_context->pix_fmt = AV_PIX_FMT_D3D11;
#endif

	_context->hw_device_ctx = _hwinst->create_device_context();

	_context->hw_frames_ctx = av_hwframe_ctx_alloc(_context->hw_device_ctx);
	if (!_context->hw_frames_ctx)
		throw std::runtime_error("Failed to allocate AVHWFramesContext.");

	AVHWFramesContext* ctx = reinterpret_cast<AVHWFramesContext*>(_context->hw_frames_ctx->data);
	ctx->width             = _context->width;
	ctx->height            = _context->height;
	ctx->format            = _context->pix_fmt;
	ctx->sw_format         = _context->sw_pix_fmt;

	if (av_hwframe_ctx_init(_context->hw_frames_ctx) < 0)
		throw std::runtime_error("Failed to initialize AVHWFramesContext.");
}

void obsffmpeg::encoder::push_free_frame(std::shared_ptr<AVFrame> frame)
{
	auto now = std::chrono::high_resolution_clock::now();
	if (_free_frames.size() > 0) {
		if ((now - _free_frames_last_used) < std::chrono::seconds(1)) {
			_free_frames.push(frame);
		}
	} else {
		_free_frames.push(frame);
		_free_frames_last_used = std::chrono::high_resolution_clock::now();
	}
}

std::shared_ptr<AVFrame> obsffmpeg::encoder::pop_free_frame()
{
	std::shared_ptr<AVFrame> frame;
	if (_free_frames.size() > 0) {
		// Re-use existing frames first.
		frame = _free_frames.top();
		_free_frames.pop();
	} else {
		if (_hwinst) {
			frame = _hwinst->allocate_frame(_context->hw_frames_ctx);
		} else {
			frame = std::shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame* frame) {
				av_frame_unref(frame);
				av_frame_free(&frame);
			});

			frame->width  = _context->width;
			frame->height = _context->height;
			frame->format = _context->pix_fmt;

			int res = av_frame_get_buffer(frame.get(), 32);
			if (res < 0) {
				throw std::runtime_error(ffmpeg::tools::get_error_description(res));
			}
		}
	}

	return frame;
}

void obsffmpeg::encoder::push_used_frame(std::shared_ptr<AVFrame> frame)
{
	_used_frames.push(frame);
}

std::shared_ptr<AVFrame> obsffmpeg::encoder::pop_used_frame()
{
	auto frame = _used_frames.front();
	_used_frames.pop();
	return frame;
}

obsffmpeg::encoder::encoder(obs_data_t* settings, obs_encoder_t* encoder, bool is_texture_encode)
    : _self(encoder), _lag_in_frames(0), _count_send_frames(0), _have_first_frame(false)
{
	// Initial set up.
	_factory = reinterpret_cast<encoder_factory*>(obs_encoder_get_type_data(_self));
	_codec   = _factory->get_avcodec();
	_handler = obsffmpeg::find_codec_handler(_codec->name);

	if (is_texture_encode) {
#ifdef WIN32
		_hwapi = std::make_shared<obsffmpeg::hwapi::d3d11>();
#endif
		_hwinst = _hwapi->create_from_obs();
	}

	// Initialize context.
	_context = avcodec_alloc_context3(_codec);
	if (!_context) {
		PLOG_ERROR("Failed to create context for encoder '%s'.", _codec->name);
		throw std::runtime_error("failed to create context");
	}

	// Create 8MB of precached Packet data for use later on.
	av_init_packet(&_current_packet);
	av_new_packet(&_current_packet, 8 * 1024 * 1024); // 8 MB precached Packet size.

	if (!is_texture_encode) {
		initialize_sw(settings);
	} else {
		try {
			initialize_hw(settings);
		} catch (...) {
			throw obsffmpeg::unsupported_gpu_exception("Initializing hardware context failed.");
		}
	}

	// Log Encoder info
	PLOG_INFO("[%s] Initializing...", _codec->name);
	if (_hwinst) {
		PLOG_INFO("[%s]   Video Input: %ldx%ld %s %s %s", _codec->name, _context->width, _context->height,
		          ffmpeg::tools::get_pixel_format_name(_context->sw_pix_fmt),
		          ffmpeg::tools::get_color_space_name(_context->colorspace),
		          _swscale.is_source_full_range() ? "Full" : "Partial");
		PLOG_INFO("[%s]   Video Output: %ldx%ld %s %s %s", _codec->name, _context->width, _context->height,
		          ffmpeg::tools::get_pixel_format_name(_context->sw_pix_fmt),
		          ffmpeg::tools::get_color_space_name(_context->colorspace),
		          _swscale.is_target_full_range() ? "Full" : "Partial");
	} else {
		PLOG_INFO("[%s]   Video Input: %ldx%ld %s %s %s", _codec->name, _swscale.get_source_width(),
		          _swscale.get_source_height(),
		          ffmpeg::tools::get_pixel_format_name(_swscale.get_source_format()),
		          ffmpeg::tools::get_color_space_name(_swscale.get_source_colorspace()),
		          _swscale.is_source_full_range() ? "Full" : "Partial");
		PLOG_INFO("[%s]   Video Output: %ldx%ld %s %s %s", _codec->name, _swscale.get_target_width(),
		          _swscale.get_target_height(),
		          ffmpeg::tools::get_pixel_format_name(_swscale.get_target_format()),
		          ffmpeg::tools::get_color_space_name(_swscale.get_target_colorspace()),
		          _swscale.is_target_full_range() ? "Full" : "Partial");
		PLOG_INFO("[%s]   GPU Selected: %d", _codec->name, obs_data_get_int(settings, ST_FFMPEG_GPU));
	}
	PLOG_INFO("[%s]   Framerate: %ld/%ld (%f FPS)", _codec->name, _context->time_base.den, _context->time_base.num,
	          static_cast<double_t>(_context->time_base.den) / static_cast<double_t>(_context->time_base.num));
	PLOG_INFO("[%s]   Custom Settings: %s", _codec->name, obs_data_get_string(settings, ST_FFMPEG_CUSTOMSETTINGS));

	// Update settings
	update(settings);

	// Initialize Encoder
	auto gctx = obsffmpeg::obs_graphics();
	int res = avcodec_open2(_context, _codec, NULL);
	if (res < 0) {
		std::stringstream sstr;
		sstr << "Initializing encoder '" << _codec->name
		     << "' failed with error: " << ffmpeg::tools::get_error_description(res) << " (code " << res << ")";
		throw std::runtime_error(sstr.str());
	}
}

obsffmpeg::encoder::~encoder()
{
	auto gctx = obsffmpeg::obs_graphics();
	if (_context) {
		// Flush encoders that require it.
		if ((_codec->capabilities & AV_CODEC_CAP_DELAY) != 0) {
			avcodec_send_frame(_context, nullptr);
			while (avcodec_receive_packet(_context, &_current_packet) >= 0) {
				avcodec_send_frame(_context, nullptr);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}

		// Close and free context.
		avcodec_close(_context);
		avcodec_free_context(&_context);
	}

	av_packet_unref(&_current_packet);

	_swscale.finalize();
}

void obsffmpeg::encoder::get_properties(obs_properties_t* props, bool hw_encode)
{
	if (_handler)
		_handler->get_properties(props, _codec, _context, hw_encode);

	obs_property_set_enabled(obs_properties_get(props, S_KEYFRAMES), false);
	obs_property_set_enabled(obs_properties_get(props, S_KEYFRAMES_INTERVALTYPE), false);
	obs_property_set_enabled(obs_properties_get(props, S_KEYFRAMES_INTERVAL_SECONDS), false);
	obs_property_set_enabled(obs_properties_get(props, S_KEYFRAMES_INTERVAL_FRAMES), false);

	obs_property_set_enabled(obs_properties_get(props, ST_FFMPEG_COLORFORMAT), false);
	obs_property_set_enabled(obs_properties_get(props, ST_FFMPEG_THREADS), false);
	obs_property_set_enabled(obs_properties_get(props, ST_FFMPEG_STANDARDCOMPLIANCE), false);
	obs_property_set_enabled(obs_properties_get(props, ST_FFMPEG_GPU), false);

}

bool obsffmpeg::encoder::update(obs_data_t* settings)
{
	// Settings
	/// Rate Control
	_context->strict_std_compliance = static_cast<int>(obs_data_get_int(settings, ST_FFMPEG_STANDARDCOMPLIANCE));
	_context->debug                 = 0;
	/// Threading
	if (_codec->capabilities & (AV_CODEC_CAP_AUTO_THREADS | AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_SLICE_THREADS)
	    && !_hwinst) {
		if (_codec->capabilities & AV_CODEC_CAP_FRAME_THREADS) {
			_context->thread_type |= FF_THREAD_FRAME;
		}
		if (_codec->capabilities & AV_CODEC_CAP_SLICE_THREADS) {
			_context->thread_type |= FF_THREAD_SLICE;
		}
		int64_t threads = obs_data_get_int(settings, ST_FFMPEG_THREADS);
		if (threads > 0) {
			_context->thread_count = static_cast<int>(threads);
			_lag_in_frames         = _context->thread_count;
		} else {
			_context->thread_count = std::thread::hardware_concurrency();
			_lag_in_frames         = _context->thread_count;
		}
	} else {
		_context->thread_count = 1;
		_context->thread_type  = 0;
		_lag_in_frames         = 1;
	}

	if (_handler)
		_handler->update(settings, _codec, _context);

	if ((_codec->capabilities & AV_CODEC_CAP_INTRA_ONLY) == 0) {
		// Key-Frame Options
		obs_video_info ovi;
		if (!obs_get_video_info(&ovi)) {
			throw std::runtime_error("no video info");
		}

		int64_t kf_type    = obs_data_get_int(settings, S_KEYFRAMES_INTERVALTYPE);
		bool    is_seconds = (kf_type == 0);

		if (is_seconds) {
			_context->gop_size = static_cast<int>(
			    obs_data_get_double(settings, S_KEYFRAMES_INTERVAL_SECONDS) * (ovi.fps_num / ovi.fps_den));
		} else {
			_context->gop_size = static_cast<int>(obs_data_get_int(settings, S_KEYFRAMES_INTERVAL_FRAMES));
		}
		_context->keyint_min = _context->gop_size;
	}

  if (!_hwinst)
	  av_opt_set_int(_context, "gpu", (int)obs_data_get_int(settings, ST_FFMPEG_GPU), AV_OPT_SEARCH_CHILDREN);

	{ // FFmpeg Custom Options
		const char* opts     = obs_data_get_string(settings, ST_FFMPEG_CUSTOMSETTINGS);
		size_t      opts_len = strnlen(opts, 65535);

		// Parses options like FFmpeg's command line would:
		// '-key=value -key=value -key=value'

		std::stack<char> quote_stack;

		bool have_param = false;
		bool have_key   = false;
		bool have_value = false;

		bool value_is_quoted = false;

		size_t p_key   = 0;
		size_t p_value = 0;

		std::string key;
		std::string value;

		for (size_t p = 0; p <= opts_len; p++) {
			char here = opts[p];

			if (!have_param) {
				if (here == '-') {
					have_param = true;
					p_key      = p + 1;
				}
			} else if (!have_key) {
				if (here == '=') {
					have_key = true;
					key      = std::string(&opts[p_key], &opts[p]);
					p_value  = p + 1;
					while (quote_stack.size() > 0)
						quote_stack.pop();
				} else if (p == opts_len) {
					PLOG_WARNING("Option '%s' has no value, ignoring.", &opts[p_key]);
				}
			} else {
				// To support quotes, some interesting stuff needs to be done.
				// First we need to parse \ as escaping the next character, and just jump over it.
				if (here == '\\') {
					p++;
				} else {
					if ((here == '"') || (here == '\'')) {
						// Dealing with quotes is only possible with a stack.
						if ((quote_stack.size() > 0) && (quote_stack.top() == here)) {
							quote_stack.pop();
						} else {
							quote_stack.push(here);
							value_is_quoted = true;
						}
					} else if ((here == ' ') || (p == opts_len)) {
						if (quote_stack.size() == 0) {
							have_value = true;
						} else if (p == opts_len) {
							PLOG_WARNING(
							    "Option '%.*s' has mismatched quotations, ignoring.",
							    p_value - p_key, &opts[p_key]);
						}
					}

					if (have_value) {
						if (value_is_quoted) {
							value = std::string(&opts[p_value + 1], &opts[p - 1]);
						} else {
							value = std::string(&opts[p_value], &opts[p]);
						}
						int ret = 0;
						if ((ret = av_opt_set(_context, key.c_str(), value.c_str(),
						                      AV_OPT_SEARCH_CHILDREN))
						    < 0) {
							PLOG_WARNING("Option '%s' could not be set to '%s'.",
							             key.c_str(), value.c_str());
						}
						have_param      = false;
						have_key        = false;
						have_value      = false;
						value_is_quoted = false;
					}
				}
			}
		}
	}

	if (_handler)
		_handler->override_lag_in_frames(_lag_in_frames, settings, _codec, _context);

	// Handler Logging
	if (_handler)
		_handler->log_options(settings, _codec, _context);

	return true;
}

void obsffmpeg::encoder::get_audio_info(audio_convert_info*) {}

size_t obsffmpeg::encoder::get_frame_size()
{
	return size_t();
}

bool obsffmpeg::encoder::audio_encode(encoder_frame*, encoder_packet*, bool*)
{
	return false;
}

void obsffmpeg::encoder::get_video_info(video_scale_info* vsi)
{
	vsi->width  = _swscale.get_source_width();
	vsi->height = _swscale.get_source_height();
	vsi->format = ffmpeg::tools::avpixelformat_to_obs_videoformat(_swscale.get_source_format());
}

bool obsffmpeg::encoder::get_sei_data(uint8_t** data, size_t* size)
{
	if (_sei_data.size() == 0)
		return false;

	*data = _sei_data.data();
	*size = _sei_data.size();
	return true;
}

bool obsffmpeg::encoder::get_extra_data(uint8_t** data, size_t* size)
{
	if (_extra_data.size() == 0)
		return false;

	*data = _extra_data.data();
	*size = _extra_data.size();
	return true;
}

static inline void copy_data(encoder_frame* frame, AVFrame* vframe)
{
	int h_chroma_shift, v_chroma_shift;
	av_pix_fmt_get_chroma_sub_sample(static_cast<AVPixelFormat>(vframe->format), &h_chroma_shift, &v_chroma_shift);

	for (size_t idx = 0; idx < MAX_AV_PLANES; idx++) {
		if (!frame->data[idx] || !vframe->data[idx])
			continue;

		size_t plane_height = vframe->height >> (idx ? v_chroma_shift : 0);

		if (static_cast<uint32_t>(vframe->linesize[idx]) == frame->linesize[idx]) {
			std::memcpy(vframe->data[idx], frame->data[idx], frame->linesize[idx] * plane_height);
		} else {
			size_t ls_in  = frame->linesize[idx];
			size_t ls_out = vframe->linesize[idx];
			size_t bytes  = ls_in < ls_out ? ls_in : ls_out;

			uint8_t* to   = vframe->data[idx];
			uint8_t* from = frame->data[idx];

			for (size_t y = 0; y < plane_height; y++) {
				std::memcpy(to, from, bytes);
				to += ls_out;
				from += ls_in;
			}
		}
	}
}

bool obsffmpeg::encoder::video_encode(encoder_frame* frame, encoder_packet* packet, bool* received_packet)
{
	std::shared_ptr<AVFrame> vframe = pop_free_frame(); // Retrieve an empty frame.

	// Convert frame.
	{
#ifdef _DEBUG
		ScopeProfiler profile("convert");
#endif

		vframe->height          = _context->height;
		vframe->format          = _context->pix_fmt;
		vframe->color_range     = _context->color_range;
		vframe->colorspace      = _context->colorspace;
		vframe->color_primaries = _context->color_primaries;
		vframe->color_trc       = _context->color_trc;
		vframe->pts             = frame->pts;

		if ((_swscale.is_source_full_range() == _swscale.is_target_full_range())
		    && (_swscale.get_source_colorspace() == _swscale.get_target_colorspace())
		    && (_swscale.get_source_format() == _swscale.get_target_format())) {
			copy_data(frame, vframe.get());
		} else {
			int res = _swscale.convert(reinterpret_cast<uint8_t**>(frame->data),
			                           reinterpret_cast<int*>(frame->linesize), 0, _context->height,
			                           vframe->data, vframe->linesize);
			if (res <= 0) {
				PLOG_ERROR("Failed to convert frame: %s (%ld).",
				           ffmpeg::tools::get_error_description(res), res);
				return false;
			}
		}
	}

	if (!encode_avframe(vframe, packet, received_packet))
		return false;

	return true;
}

bool obsffmpeg::encoder::video_encode_texture(uint32_t handle, int64_t pts, uint64_t lock_key, uint64_t* next_lock_key,
                                              encoder_packet* packet, bool* received_packet)
{
	if (handle == GS_INVALID_HANDLE) {
		PLOG_ERROR("Received invalid handle.");
		*next_lock_key = lock_key;
		return false;
	}

	std::shared_ptr<AVFrame> vframe = pop_free_frame();
	_hwinst->copy_from_obs(_context->hw_frames_ctx, handle, lock_key, next_lock_key, vframe);

	vframe->color_range     = _context->color_range;
	vframe->colorspace      = _context->colorspace;
	vframe->color_primaries = _context->color_primaries;
	vframe->color_trc       = _context->color_trc;
	vframe->pts             = pts;

	if (!encode_avframe(vframe, packet, received_packet))
		return false;

	*next_lock_key = lock_key;

	return true;
}

int obsffmpeg::encoder::receive_packet(bool* received_packet, struct encoder_packet* packet)
{
	auto gctx = obsffmpeg::obs_graphics();
	av_packet_unref(&_current_packet);

	int res = avcodec_receive_packet(_context, &_current_packet);
	if (res != 0) {
		return res;
	}

	if (!_have_first_frame) {
		if (_codec->id == AV_CODEC_ID_H264) {
			uint8_t* tmp_packet;
			uint8_t* tmp_header;
			uint8_t* tmp_sei;
			size_t   sz_packet, sz_header, sz_sei;

			obs_extract_avc_headers(_current_packet.data, _current_packet.size, &tmp_packet, &sz_packet,
			                        &tmp_header, &sz_header, &tmp_sei, &sz_sei);

			if (sz_header) {
				_extra_data.resize(sz_header);
				std::memcpy(_extra_data.data(), tmp_header, sz_header);
			}

			if (sz_sei) {
				_sei_data.resize(sz_sei);
				std::memcpy(_sei_data.data(), tmp_sei, sz_sei);
			}

			// Not required, we only need the Extra Data and SEI Data anyway.
			//std::memcpy(_current_packet.data, tmp_packet, sz_packet);
			//_current_packet.size = static_cast<int>(sz_packet);

			bfree(tmp_packet);
			bfree(tmp_header);
			bfree(tmp_sei);
		} else if (_codec->id == AV_CODEC_ID_HEVC) {
			obsffmpeg::codecs::hevc::extract_header_sei(_current_packet.data, _current_packet.size,
			                                            _extra_data, _sei_data);
		} else if (_context->extradata != nullptr) {
			_extra_data.resize(_context->extradata_size);
			std::memcpy(_extra_data.data(), _context->extradata, _context->extradata_size);
		}
		_have_first_frame = true;
	}

	// Allow Handler Post-Processing
	if (_handler)
		_handler->process_avpacket(_current_packet, _codec, _context);

	packet->type          = OBS_ENCODER_VIDEO;
	packet->pts           = _current_packet.pts;
	packet->dts           = _current_packet.dts;
	packet->data          = _current_packet.data;
	packet->size          = _current_packet.size;
	packet->keyframe      = !!(_current_packet.flags & AV_PKT_FLAG_KEY);
	packet->drop_priority = packet->keyframe ? 0 : 1;
	*received_packet      = true;

	push_free_frame(pop_used_frame());

	return res;
}

int obsffmpeg::encoder::send_frame(std::shared_ptr<AVFrame> const frame)
{
	auto gctx = obsffmpeg::obs_graphics();
	int res = avcodec_send_frame(_context, frame.get());
	if (res == 0) {
		push_used_frame(frame);
	}

	return res;
}

bool obsffmpeg::encoder::encode_avframe(std::shared_ptr<AVFrame> frame, encoder_packet* packet, bool* received_packet)
{
#ifdef _DEBUG
	ScopeProfiler profile("loop");
#endif

	bool sent_frame  = false;
	bool recv_packet = false;
	bool should_lag  = (_count_send_frames >= _lag_in_frames);

	auto loop_begin = std::chrono::high_resolution_clock::now();
	auto loop_end   = loop_begin + std::chrono::milliseconds(50);

	while ((!sent_frame || (should_lag && !recv_packet))
	       && !(std::chrono::high_resolution_clock::now() > loop_end)) {
		bool eagain_is_stupid = false;

		if (!sent_frame) {
#ifdef _DEBUG
			ScopeProfiler profile_inner("send");
#endif
			int res = send_frame(frame);
			switch (res) {
			case 0:
				sent_frame = true;
				frame      = nullptr;
				break;
			case AVERROR(EAGAIN):
				// This means we should call receive_packet again, but what do we do with that data?
				// Why can't we queue on both? Do I really have to implement threading for this stuff?
				if (*received_packet == true) {
					PLOG_WARNING("Skipped frame due to EAGAIN when a packet was already returned.");
					sent_frame = true;
				}
				eagain_is_stupid = true;
				break;
			case AVERROR(EOF):
				PLOG_ERROR("Skipped frame due to end of stream.");
				sent_frame = true;
				break;
			default:
				PLOG_ERROR("Failed to encode frame: %s (%ld).",
				           ffmpeg::tools::get_error_description(res), res);
				return false;
			}
		}

		if (!recv_packet) {
#ifdef _DEBUG
			ScopeProfiler profile_inner("recieve");
#endif
			int res = receive_packet(received_packet, packet);
			switch (res) {
			case 0:
				recv_packet = true;
				break;
			case AVERROR(EOF):
				PLOG_ERROR("Received end of file.");
				recv_packet = true;
				break;
			case AVERROR(EAGAIN):
				if (sent_frame) {
					recv_packet = true;
				}
				if (eagain_is_stupid) {
					PLOG_ERROR("Both send and recieve returned EAGAIN, encoder is broken.");
					return false;
				}
				break;
			default:
				PLOG_ERROR("Failed to receive packet: %s (%ld).",
				           ffmpeg::tools::get_error_description(res), res);
				return false;
			}
		}

		if (!sent_frame || !recv_packet) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	if (!sent_frame)
		push_free_frame(frame);

	return true;
}
