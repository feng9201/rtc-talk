#include "encodeG711.h"
#include "g711.h"


namespace G711 {
	#define PCM_MAX  (32635)

	#define	BIAS		(0x84)	 /* Bias for linear code. 线性码偏移值*/
	#define CLIP         8159    //最大量化级数量

	bool binit = false;

	static short seg_end[8] = { 0xFF, 0x1FF, 0x3FF, 0x7FF,
	0xFFF, 0x1FFF, 0x3FFF, 0x7FFF };  //分成不均匀的8个分段，算上负数，总共是16个分段

	static int seg_uend[8] = { 0x3F, 0x7F, 0xFF, 0x1FF,
	0x3FF, 0x7FF, 0xFFF, 0x1FFF };

	unsigned short alaw_to_linear[256];
	unsigned short ulaw_to_linear[256];

	static short search(short val, short* table, short size)
	{
		short i;
		for (i = 0; i < size; i++)
		{
			if (val <= *table++)     //找出一个与输入信号最相近的值（大于）
			{
				return (i);
			}
		}
		return (size);
	}

	static int search(
		int val,	/* changed from "short" *drago* */
		int* table,
		int size)	/* changed from "short" *drago* */
	{
		int i;		/* changed from "short" *drago* */

		for (i = 0; i < size; i++) {
			if (val <= *table++)
				return (i);
		}
		return (size);
	}

	static char linear2ulaw(short pcm_val)	/* 2's complement (16-bit range) */
	{
		int		mask;
		int		seg;
		unsigned char uval;

		/* Get the sign and the magnitude of the value. */
		pcm_val = pcm_val >> 2;
		if (pcm_val < 0) {
			pcm_val = -pcm_val;
			mask = 0x7F;
		}
		else {
			mask = 0xFF;
		}
		if (pcm_val > CLIP) pcm_val = CLIP;		/* clip the magnitude 削波*/
		pcm_val += (BIAS >> 2);

		/* Convert the scaled magnitude to segment number. */
		seg = search(pcm_val, seg_uend, 8);

		/*
		* Combine the sign, segment, quantization bits;
		* and complement the code word.
		*/
		if (seg >= 8)		/* out of range, return maximum value. */
			return (0x7F ^ mask);
		else {
			uval = (seg << 4) | ((pcm_val >> (seg + 1)) & 0xF);
			return (uval ^ mask);
		}
	}

	int G711aEncode(char* dst, const short* src, size_t len)
	{
		if (!src) {
			return 0;
		}
		for (int i = 0; i < len; i++){
			short pcm = *src++;
			int sign = (pcm & 0x8000) >> 8;
			if (sign != 0)
				pcm = -pcm;
			if (pcm > PCM_MAX)
				pcm = PCM_MAX;
			int exponent = 7;
			int expMask;
			for (expMask = 0x4000; (pcm & expMask) == 0 && exponent > 0; exponent--, expMask >>= 1) {}
			int mantissa = (pcm >> ((exponent == 0) ? 4 : (exponent + 3))) & 0x0f;
			unsigned char alaw = (unsigned char)(sign | exponent << 4 | mantissa);
			*dst++ = (char)(alaw ^ 0xD5);
		}
		return len/2;
	}

	void G711uEncode(char* dst, short* src, size_t len)
	{
		for (int i = 0; i < len; i++){
			short pcm = *src++;
			*dst++ = linear2ulaw(pcm);
		}
	}

	static void build_xlaw_to_linear_table(unsigned short* xlaw_to_linear,
		short (*xlaw2linear)(unsigned char))
	{
		int i;

		for (i = 0; i < 256; i++) {
			xlaw_to_linear[i] = (unsigned short)xlaw2linear(i);
		}
	}

	static void xlaw_to_pcm16(unsigned short* xlaw_to_linear, int src_length, const char* src_samples, char* dst_samples)
	{
		int i;
		unsigned char* s_samples;
		unsigned short* d_samples;

		s_samples = (unsigned char*)src_samples;
		d_samples = (unsigned short*)dst_samples;

		for (i = 0; i < src_length; i++)
		{
			d_samples[i] = xlaw_to_linear[s_samples[i]];
		}
	}

	void alaw_to_pcm16(const char* src_samples, char* dst_samples, int src_length)
	{
		if (!binit) {
			build_xlaw_to_linear_table(alaw_to_linear, alaw2linear);
			binit = true;
		}
		xlaw_to_pcm16(alaw_to_linear, src_length, src_samples, dst_samples);
	}

	void ulaw_to_pcm16(const char* src_samples, char* dst_samples, int src_length)
	{
		if (!binit) {
			build_xlaw_to_linear_table(ulaw_to_linear, ulaw2linear);
			binit = true;
		}
		xlaw_to_pcm16(ulaw_to_linear, src_length, src_samples, dst_samples);
	}
}