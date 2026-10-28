#pragma once
/*
* @brief: g711编解码
*/

namespace G711{
	int  G711aEncode(char* dst, const short* src, size_t len);
	void G711uEncode(char* dst, short* src, size_t len);

	void alaw_to_pcm16(const char* src_samples, char* dst_samples, int src_length);
	void ulaw_to_pcm16(const char* src_samples, char* dst_samples, int src_length);
}
