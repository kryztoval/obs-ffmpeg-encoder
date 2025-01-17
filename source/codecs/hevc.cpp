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

#include "hevc.hpp"
#include "utility.hpp"

enum class nal_unit_type : uint8_t { // 6 bits
	TRAIL_N        = 0,
	TRAIL_R        = 1,
	TSA_N          = 2,
	TSA_R          = 3,
	STSA_N         = 4,
	STSA_R         = 5,
	RADL_N         = 6,
	RADL_R         = 7,
	RASL_N         = 8,
	RASL_R         = 9,
	RSV_VCL_N10    = 10,
	RSV_VCL_R11    = 11,
	RSV_VCL_N12    = 12,
	RSV_VCL_R13    = 13,
	RSV_VCL_N14    = 14,
	RSV_VCL_R15    = 15,
	BLA_W_LP       = 16,
	BLA_W_RADL     = 17,
	BLA_N_LP       = 18,
	IDR_W_RADL     = 19,
	IDR_N_LP       = 20,
	CRA            = 21,
	RSV_IRAP_VCL22 = 22,
	RSV_IRAP_VCL23 = 23,
	RSV_VCL24      = 24,
	RSV_VCL25      = 25,
	RSV_VCL26      = 26,
	RSV_VCL27      = 27,
	RSV_VCL28      = 28,
	RSV_VCL29      = 29,
	RSV_VCL30      = 30,
	RSV_VCL31      = 31,
	VPS            = 32,
	SPS            = 33,
	PPS            = 34,
	AUD            = 35,
	EOS            = 36,
	EOB            = 37,
	FD             = 38,
	PREFIX_SEI     = 39,
	SUFFIX_SEI     = 40,
	RSV_NVCL41     = 41,
	RSV_NVCL42     = 42,
	RSV_NVCL43     = 43,
	RSV_NVCL44     = 44,
	RSV_NVCL45     = 45,
	RSV_NVCL46     = 46,
	RSV_NVCL47     = 47,
	UNSPEC48       = 48,
	UNSPEC49       = 49,
	UNSPEC50       = 50,
	UNSPEC51       = 51,
	UNSPEC52       = 52,
	UNSPEC53       = 53,
	UNSPEC54       = 54,
	UNSPEC55       = 55,
	UNSPEC56       = 56,
	UNSPEC57       = 57,
	UNSPEC58       = 58,
	UNSPEC59       = 59,
	UNSPEC60       = 60,
	UNSPEC61       = 61,
	UNSPEC62       = 62,
	UNSPEC63       = 63,
};

struct hevc_nal_unit_header {
	bool          zero_bit : 1;
	nal_unit_type nut : 6;
	uint8_t       layer_id : 6;
	uint8_t       temporal_id_plus1 : 3;
};

struct hevc_nal {
	hevc_nal_unit_header* header;
	size_t                size = 0;
	uint8_t*              data = nullptr;
};

bool is_nal(uint8_t* data, uint8_t* end)
{
	size_t s = end - data;
	if (s < 4)
		return false;

	if (*data != 0x0)
		return false;
	if (*(data + 1) != 0x0)
		return false;
	if (*(data + 2) != 0x0)
		return false;
	if (*(data + 3) != 0x1)
		return false;

	return true;
}

bool seek_to_nal(uint8_t*& data, uint8_t* end)
{
	if (data > end)
		return false;

	for (; data <= end; data++) {
		if (is_nal(data, end)) {
			return true;
		}
	}

	return false;
}

size_t get_nal_size(uint8_t* data, uint8_t* end)
{
	uint8_t* ptr = data + 4;
	if (!seek_to_nal(ptr, end)) {
		return end - data;
	}
	return ptr - data;
}

bool is_discard_marker(uint8_t* data, uint8_t* end)
{
	size_t s = end - data;
	if (s < 4)
		return false;

	if (*data != 0x0)
		return false;
	if (*(data + 1) != 0x0)
		return false;

	if (*(data + 2) == 0x3) {
		// Discard marker only if the next byte is not 0x0, 0x1, 0x2 or 0x3.
		if (*(data + 3) != 0x0)
			return false;
		if (*(data + 3) != 0x1)
			return false;
		if (*(data + 3) != 0x2)
			return false;
		if (*(data + 3) != 0x3)
			return false;

		return true;
	} else {
		if (*(data + 2) == 0x0)
			return true;
		if (*(data + 2) == 0x1)
			return true;
		if (*(data + 2) == 0x2)
			return true;

		return false;
	}
}

bool should_discard_nal(uint8_t* data, uint8_t* end)
{
	if (data > end)
		return true;

	for (; data <= end; data++) {
		if (is_discard_marker(data, end))
			return true;
	}

	return false;
}

void progress_parse(uint8_t*& ptr, uint8_t* end, size_t& sz)
{
	ptr += sz;
	sz = get_nal_size(ptr, end);
}

void obsffmpeg::codecs::hevc::extract_header_sei(uint8_t* data, size_t sz_data,
                                                 std::vector<uint8_t>& header, std::vector<uint8_t>& sei)
{
	uint8_t* ptr = data;
	uint8_t* end = data + sz_data;

	// Reserve enough memory to store the entire packet data if necessary.
	header.reserve(sz_data);
	sei.reserve(sz_data);

	if (!seek_to_nal(ptr, end)) {
		return;
	}

	for (size_t nal_sz = get_nal_size(ptr, end); nal_sz > 0; progress_parse(ptr, end, nal_sz)) {
		if (should_discard_nal(ptr + 4, ptr + nal_sz)) {
			continue;
		}

		hevc_nal nal;
		nal.header = reinterpret_cast<hevc_nal_unit_header*>(ptr + 4);
		nal.size   = nal_sz - 4 - 2;
		nal.data   = ptr + 4 + 2;

		switch (nal.header->nut) {
		case nal_unit_type::VPS:
		case nal_unit_type::SPS:
		case nal_unit_type::PPS:
			header.insert(header.end(), ptr, ptr + nal_sz);
			break;
		case nal_unit_type::PREFIX_SEI:
		case nal_unit_type::SUFFIX_SEI:
			sei.insert(sei.end(), ptr, ptr + nal_sz);
			break;
		}
	}
}
