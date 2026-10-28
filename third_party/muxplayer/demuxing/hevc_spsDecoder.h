#pragma once

#include <vector>
#include <cstdint>
#include <cstring>

// 平台无关的类型定义
#ifdef _WIN32
#include <windows.h>
typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned long uint32;
typedef unsigned long long uint64;
typedef signed char int8;
typedef signed short int16;
typedef signed long int32;
typedef signed long long int64;
#else
#include <cstdint>
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int8_t int8;
typedef int32_t int32;
typedef int64_t int64;
#endif

typedef unsigned char* LPBYTE;

struct vc_params_t
{
    int width, height;
    int profile, level;
    int nal_length_size;
    int max_ref_frames;  // 新增：最大参考帧数

    void clear()
    {
        width = height = 0;
        profile = level = 0;
        nal_length_size = 0;
        max_ref_frames = 0;  // 新增
    }
};

class NALBitstream
{
public:
    NALBitstream() : m_data(NULL), m_len(0), m_idx(0), m_bits(0), m_byte(0), m_zeros(0) {};

    NALBitstream(const void* data, int len)
    {
        Init(data, len);
    }

    void Init(const void* data, int len)
    {
        m_data = (const uint8*)data;
        m_len = len;
        m_idx = 0;
        m_bits = 0;
        m_byte = 0;
        m_zeros = 0;
    }

    // 线程安全的读取方法
    uint8 GetBYTE()
    {
        if (m_idx >= m_len)
            return 0;

        uint8 b = m_data[m_idx++];

        // 避免起始码仿真，在00 00后插入0x03字节，这里需要丢弃
        if (b == 0)
        {
            m_zeros++;
            if ((m_idx < m_len) && (m_zeros == 2) && (m_data[m_idx] == 0x03))
            {
                m_idx++;
                m_zeros = 0;
            }
        }
        else {
            m_zeros = 0;
        }
        return b;
    }

    uint32 GetBit()
    {
        if (m_bits == 0)
        {
            m_byte = GetBYTE();
            m_bits = 8;
        }
        m_bits--;
        return (m_byte >> m_bits) & 0x1;
    }

    uint32 GetWord(int bits)
    {
        uint32 u = 0;
        while (bits > 0)
        {
            u <<= 1;
            u |= GetBit();
            bits--;
        }
        return u;
    }

    uint32 GetUE()
    {
        int zeros = 0;
        while (m_idx < m_len && GetBit() == 0)
            zeros++;
        return GetWord(zeros) + ((1 << zeros) - 1);
    }

    int32 GetSE()
    {
        uint32 UE = GetUE();
        bool positive = UE & 1;
        uint32 SE = (UE + 1) >> 1;
        if (!positive)
        {
            SE = -static_cast<int32>(SE);
        }
        return static_cast<int32>(SE);
    }

    // 获取当前读取位置
    int GetPosition() const { return m_idx; }

    // 获取剩余长度
    int GetRemaining() const { return m_len - m_idx; }

private:
    const uint8* m_data;
    int m_len;
    int m_idx;
    int m_bits;
    uint8 m_byte;
    int m_zeros;
};

/**
 * 从H.265流中提取SPS（线程安全版本）
 */
bool GetH265Sps(const uint8_t* stream, size_t streamSize, std::vector<uint8_t>& sps)
{
    if (!stream || streamSize < 5) {
        return false;
    }

    sps.clear();

    size_t i = 0;
    while (i < streamSize - 4) {
        if (stream[i] == 0x00 && stream[i + 1] == 0x00) {
            if (stream[i + 2] == 0x01) {
                uint8_t nalType = (stream[i + 3] >> 1) & 0x3F;

                if (nalType == 33) {
                    size_t start = i + 3;
                    size_t j = start + 1;

                    while (j < streamSize - 3) {
                        if (stream[j] == 0x00 && stream[j + 1] == 0x00 &&
                            (stream[j + 2] == 0x01 || (j + 3 < streamSize && stream[j + 2] == 0x00 && stream[j + 3] == 0x01))) {
                            break;
                        }
                        j++;
                    }

                    if (j > streamSize) j = streamSize;
                    sps.assign(stream + start, stream + j);
                    return true;
                }
                i += 3;
            }
            else if (i + 3 < streamSize && stream[i + 2] == 0x00 && stream[i + 3] == 0x01) {
                uint8_t nalType = (stream[i + 4] >> 1) & 0x3F;

                if (nalType == 33) {
                    size_t start = i + 4;
                    size_t j = start + 1;

                    while (j < streamSize - 4) {
                        if (stream[j] == 0x00 && stream[j + 1] == 0x00 &&
                            (stream[j + 2] == 0x01 || (j + 3 < streamSize && stream[j + 2] == 0x00 && stream[j + 3] == 0x01))) {
                            break;
                        }
                        j++;
                    }

                    if (j > streamSize) j = streamSize;
                    sps.assign(stream + start, stream + j);
                    return true;
                }
                i += 4;
            }
            else {
                i++;
            }
        }
        else {
            i++;
        }
    }

    return false;
}

/**
 * 解析序列参数集（线程安全版本）- 新增最大参考帧解析
 *
 * 说明(对照 H.264 的 h264_decode_sps bug):
 *   H.265 SPS 字段顺序与 H.264 不同, width/height/max_ref_frames 等
 *   关键字段位于 scaling_list 之前。本函数解析到
 *   log2_diff_max_min_luma_coding_block_size 即 return true,
 *   未走到 scaling_list_data 部分, 因此不会像 H.264 那样因漏解析
 *   scaling matrix 而导致后续字段 bit 错位。H.265 下 width/height/
 *   max_ref_frames 始终解析正常。
 */
bool ParseSequenceParameterSet(const uint8* data, int size, vc_params_t& params)
{
    if (!data || size < 20)
    {
        return false;
    }

    params.clear();

    NALBitstream bs(data, size);

    try {
        // seq_parameter_set_rbsp()
        bs.GetWord(4); // sps_video_parameter_set_id
        int sps_max_sub_layers_minus1 = bs.GetWord(3);
        if (sps_max_sub_layers_minus1 > 6)
        {
            return false;
        }
        bs.GetWord(1); // sps_temporal_id_nesting_flag

        // profile_tier_level
        {
            bs.GetWord(2); // general_profile_space
            bs.GetWord(1); // general_tier_flag
            params.profile = bs.GetWord(5); // general_profile_idc
            bs.GetWord(32); // general_profile_compatibility_flag[32]
            bs.GetWord(1); // general_progressive_source_flag
            bs.GetWord(1); // general_interlaced_source_flag
            bs.GetWord(1); // general_non_packed_constraint_flag
            bs.GetWord(1); // general_frame_only_constraint_flag
            bs.GetWord(44); // general_reserved_zero_44bits
            params.level = bs.GetWord(8); // general_level_idc

            std::vector<uint8> sub_layer_profile_present_flag(sps_max_sub_layers_minus1);
            std::vector<uint8> sub_layer_level_present_flag(sps_max_sub_layers_minus1);

            for (int i = 0; i < sps_max_sub_layers_minus1; i++) {
                sub_layer_profile_present_flag[i] = bs.GetWord(1);
                sub_layer_level_present_flag[i] = bs.GetWord(1);
            }

            if (sps_max_sub_layers_minus1 > 0) {
                for (int i = sps_max_sub_layers_minus1; i < 8; i++) {
                    bs.GetWord(2); // reserved_zero_2bits
                }
            }

            for (int i = 0; i < sps_max_sub_layers_minus1; i++) {
                if (sub_layer_profile_present_flag[i]) {
                    bs.GetWord(2); // sub_layer_profile_space[i]
                    bs.GetWord(1); // sub_layer_tier_flag[i]
                    bs.GetWord(5); // sub_layer_profile_idc[i]
                    bs.GetWord(32); // sub_layer_profile_compatibility_flag[i][32]
                    bs.GetWord(1); // sub_layer_progressive_source_flag[i]
                    bs.GetWord(1); // sub_layer_interlaced_source_flag[i]
                    bs.GetWord(1); // sub_layer_non_packed_constraint_flag[i]
                    bs.GetWord(1); // sub_layer_frame_only_constraint_flag[i]
                    bs.GetWord(44); // sub_layer_reserved_zero_44bits[i]
                }

                if (sub_layer_level_present_flag[i]) {
                    bs.GetWord(8); // sub_layer_level_idc[i]
                }
            }
        }

        uint32 sps_seq_parameter_set_id = bs.GetUE();
        if (sps_seq_parameter_set_id > 15) {
            return false;
        }

        uint32 chroma_format_idc = bs.GetUE();
        if (chroma_format_idc > 3) {
            return false;
        }

        if (chroma_format_idc == 3) {
            bs.GetWord(1); // separate_colour_plane_flag
        }

        params.width = bs.GetUE(); // pic_width_in_luma_samples
        params.height = bs.GetUE(); // pic_height_in_luma_samples

        if (bs.GetWord(1)) { // conformance_window_flag
            bs.GetUE(); // conf_win_left_offset
            bs.GetUE(); // conf_win_right_offset
            bs.GetUE(); // conf_win_top_offset
            bs.GetUE(); // conf_win_bottom_offset
        }

        uint32 bit_depth_luma_minus8 = bs.GetUE();
        uint32 bit_depth_chroma_minus8 = bs.GetUE();

        if (bit_depth_luma_minus8 != bit_depth_chroma_minus8) {
            return false;
        }

        uint32 log2_max_pic_order_cnt_lsb_minus4 = bs.GetUE();
        
        // ========== 新增：解析参考帧相关参数 ==========
        uint32 sps_sub_layer_ordering_info_present_flag = bs.GetWord(1);
        
        // 解析每个子层的参考帧信息
        for (int i = (sps_sub_layer_ordering_info_present_flag ? 0 : sps_max_sub_layers_minus1); 
             i <= sps_max_sub_layers_minus1; i++) {
            
            uint32 sps_max_dec_pic_buffering_minus1 = bs.GetUE();
            uint32 sps_max_num_reorder_pics = bs.GetUE();
            uint32 sps_max_latency_increase_plus1 = bs.GetUE();
            
            // 保存最高子层的最大参考帧数（通常这是实际使用的值）
            if (i == sps_max_sub_layers_minus1) {
                // sps_max_dec_pic_buffering_minus1 + 1 就是最大参考帧数
                params.max_ref_frames = sps_max_dec_pic_buffering_minus1 + 1;
            }
        }
        // ========== 参考帧解析结束 ==========

        // 继续解析其他可能需要的参数...
        uint32 log2_min_luma_coding_block_size_minus3 = bs.GetUE();
        uint32 log2_diff_max_min_luma_coding_block_size = bs.GetUE();
        
        // 如果还需要其他参数，可以继续解析...

        return true;
    }
    catch (...) {
        return false;
    }
}
