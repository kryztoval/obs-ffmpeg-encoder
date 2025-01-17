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

#pragma once
#include <map>
#include "utility.hpp"

extern "C" {
#include <obs-properties.h>
#pragma warning(push)
#pragma warning(disable : 4244)
#include <libavcodec/avcodec.h>
#pragma warning(pop)
}

namespace obsffmpeg {
	namespace nvenc {
		enum class preset : int64_t {
			DEFAULT,
			SLOW,
			MEDIUM,
			FAST,
			HIGH_PERFORMANCE,
			HIGH_QUALITY,
			BLURAYDISC,
			LOW_LATENCY,
			LOW_LATENCY_HIGH_PERFORMANCE,
			LOW_LATENCY_HIGH_QUALITY,
			LOSSLESS,
			LOSSLESS_HIGH_PERFORMANCE,
		};

		enum class ratecontrolmode : int64_t {
			CQP,
			VBR,
			VBR_HQ,
			CBR,
			CBR_HQ,
			CBR_LD_HQ,
		};

		enum class b_ref_mode : int64_t {
			DISABLED,
			EACH,
			MIDDLE,
		};

		extern std::map<preset, std::string> presets;

		extern std::map<preset, std::string> preset_to_opt;

		extern std::map<ratecontrolmode, std::string> ratecontrolmodes;

		extern std::map<ratecontrolmode, std::string> ratecontrolmode_to_opt;

		extern std::map<b_ref_mode, std::string> b_ref_modes;

		extern std::map<b_ref_mode, std::string> b_ref_mode_to_opt;

		void override_lag_in_frames(size_t& lag, obs_data_t* settings, const AVCodec* codec,
		                            AVCodecContext* context);

		void get_defaults(obs_data_t* settings, const AVCodec* codec, AVCodecContext* context);

		void get_properties_pre(obs_properties_t* props, const AVCodec* codec);

		void get_properties_post(obs_properties_t* props, const AVCodec* codec);

		void get_runtime_properties(obs_properties_t* props, const AVCodec* codec, AVCodecContext* context);

		void update(obs_data_t* settings, const AVCodec* codec, AVCodecContext* context);

		void log_options(obs_data_t* settings, const AVCodec* codec, AVCodecContext* context);
	} // namespace nvenc
} // namespace obsffmpeg
