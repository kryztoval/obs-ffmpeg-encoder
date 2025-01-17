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

#include "handler.hpp"

void obsffmpeg::ui::handler::override_visible_name(const AVCodec*, std::string&) {}

void obsffmpeg::ui::handler::override_info(obs_encoder_info*, obs_encoder_info*) {}

void obsffmpeg::ui::handler::override_colorformat(AVPixelFormat&, obs_data_t*, const AVCodec*, AVCodecContext*) {}

void obsffmpeg::ui::handler::override_lag_in_frames(size_t&, obs_data_t*, const AVCodec*, AVCodecContext*) {}

void obsffmpeg::ui::handler::get_defaults(obs_data_t*, const AVCodec*, AVCodecContext*, bool) {}

void obsffmpeg::ui::handler::get_properties(obs_properties_t*, const AVCodec*, AVCodecContext*, bool) {}

obsffmpeg::hwapi::device obsffmpeg::ui::handler::find_hw_device(std::shared_ptr<obsffmpeg::hwapi::base>, const AVCodec*,
                                                                AVCodecContext*)
{
	return obsffmpeg::hwapi::device();
}

void obsffmpeg::ui::handler::update(obs_data_t*, const AVCodec*, AVCodecContext*) {}

void obsffmpeg::ui::handler::log_options(obs_data_t*, const AVCodec*, AVCodecContext*) {}

void obsffmpeg::ui::handler::import_from_ffmpeg(const std::string, obs_data_t*, const AVCodec*, AVCodecContext*) {}

std::string obsffmpeg::ui::handler::export_for_ffmpeg(obs_data_t*, const AVCodec*, AVCodecContext*)
{
	return std::string();
}

void obsffmpeg::ui::handler::process_avpacket(AVPacket&, const AVCodec*, AVCodecContext*) {}
