#include <vector>
#include <cstring>
#include <memory>



namespace demuxUtils {

    union endian {
        uint32_t i;
        char c[4];
    };

    static endian e = { 1 };
    static bool isBigEndian = e.c[0] == 0;

    template <class T>
    static T valueFromBtye(char* in, const size_t length_ = -1) noexcept(false) {

        size_t length = (length_ == -1 ? sizeof(T) : length_);

        char* bytes = in;
        T sum = 0;
        for (int i = 0; i < length; ++i) {
            if (isBigEndian)
                sum += ((uint8_t) * (bytes + i) << (8 * i));
            else
                sum += ((uint8_t) * (bytes + i) << (8 * (length - i - 1)));
        }
        return sum;
    }

    template<class T>
    static void valueToByte(T value, char* in, const size_t length_ = -1) noexcept(false) {

        size_t length = (length_ == -1 ? sizeof(T) : length_);

        char* bytes = in;
        for (int i = 0; i < length; ++i) {
            if (isBigEndian)
                *(bytes + i) = (value >> (8 * i)) & 0xff;
            else
                *(bytes + i) = (value >> (8 * (length - i - 1))) & 0xff;
        }
    }
}// namespace demuxUtils


const uint32_t HEVC_SEQURCE_HEADER_OFFSET = 23;
const uint32_t H264_SEQURCE_HEADER_OFFSET = 5;

uint32_t
ParserSequenceHeader(const uint8_t *src, std::unique_ptr<uint8_t> &dst, uint32_t SequencHeaderSize, bool hevc) {
    dst.reset();
    uint32_t headerOffset = hevc? HEVC_SEQURCE_HEADER_OFFSET : H264_SEQURCE_HEADER_OFFSET;
    uint32_t offset = hevc ? 3:1;
    std::vector<std::pair<uint8_t*,uint32_t>> naluVec;
    uint8_t* p = const_cast<uint8_t*>(src) + headerOffset;
    uint32_t naluSize = 0;
    uint32_t allnaluSize = 0;
    uint32_t value = 0x00000001;
    while (p - src < SequencHeaderSize) {
        naluSize = demuxUtils::valueFromBtye<uint16_t>((char*)(p+=offset), sizeof(uint16_t));
        if (p-src + naluSize >= SequencHeaderSize) break;
        naluVec.push_back({p + sizeof(uint16_t), naluSize});
        allnaluSize += (naluSize + 4);
        p += (naluSize + sizeof(uint16_t));
    }
    if (!allnaluSize)
        return 0;
    dst.reset(new uint8_t[allnaluSize]{0});
    p = dst.get();
    for (auto iter : naluVec) {
        demuxUtils::valueToByte<uint32_t>(value,(char*)p,sizeof(value));
        p += 4;
        memcpy(p, iter.first, iter.second);
        p += iter.second;
    }

    return allnaluSize;
}

uint32_t ParserData(uint8_t *data, const uint32_t &size_) {
    char* p = (char*)data;
    char* last_p = p;
    uint32_t size = 0;
    uint32_t value = 0x00000001;
    while (p - (char*)data + 4 < size_) {
        size = demuxUtils::valueFromBtye<uint32_t>(p);
        demuxUtils::valueToByte<uint32_t>(value, p);
        last_p = p;
        p += (4 + size);
    }
    return last_p-(char*)data;
}

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <cmath>

typedef  unsigned int UINT;
typedef  unsigned char BYTE;
typedef  unsigned long DWORD;

namespace sps_parse
{
    UINT Ue(BYTE* pBuff, UINT nLen, UINT& nStartBit)
    {
        //计算0bit的个数
        UINT nZeroNum = 0;
        while (nStartBit < nLen * 8){
            if (pBuff[nStartBit / 8] & (0x80 >> (nStartBit % 8))){ //&:按位与，%取余
                break;
            }
            nZeroNum++;
            nStartBit++;
        }
        nStartBit++;
        //计算结果
        DWORD dwRet = 0;
        for (UINT i = 0; i < nZeroNum; i++){
            dwRet <<= 1;
            if (pBuff[nStartBit / 8] & (0x80 >> (nStartBit % 8))){
                dwRet += 1;
            }
            nStartBit++;
        }
        return (1 << nZeroNum) - 1 + dwRet;
    }
    int Se(BYTE* pBuff, UINT nLen, UINT& nStartBit)
    {
        int UeVal = Ue(pBuff, nLen, nStartBit);
        double k = UeVal;
        int nValue = ceil(k / 2);
        if (UeVal % 2 == 0)
            nValue = -nValue;
        return nValue;
    }

    DWORD u(UINT BitCount, BYTE* buf, UINT& nStartBit)
    {
        DWORD dwRet = 0;
        for (UINT i = 0; i < BitCount; i++){
            dwRet <<= 1;
            if (buf[nStartBit / 8] & (0x80 >> (nStartBit % 8))){
                dwRet += 1;
            }
            nStartBit++;
        }
        return dwRet;
    }

    /**
    * 解析scaling_list语法元素(H.264 7.3.2.1.1)
    * 当seq_scaling_list_present_flag[i]=1时,需调用本函数跳过delta_scale序列,
    * 否则后续SPS字段(log2_max_frame_num/poc_type/width/height等)bit位全部错位
    * @param buf SPS数据内容
    * @param nLen SPS数据长度(字节)
    * @param nStartBit 当前bit偏移
    * @param sizeOfScalingList 16(list 0-5) 或 8(list 6-7)
    */
    void scaling_list(BYTE* buf, UINT nLen, UINT& nStartBit, UINT sizeOfScalingList)
    {
        int lastScale = 8;
        int nextScale = 8;
        for (UINT j = 0; j < sizeOfScalingList; j++) {
            if (nextScale != 0) {
                int delta_scale = Se(buf, nLen, nStartBit);
                nextScale = (lastScale + delta_scale + 256) % 256;
            }
            lastScale = (nextScale == 0) ? lastScale : nextScale;
        }
    }

    /**
    * H264的NAL起始码防竞争机制
    * @param buf SPS数据内容
    */
    void de_emulation_prevention(BYTE* buf, unsigned int* buf_size)
    {
        int i = 0, j = 0;
        BYTE* tmp_ptr = nullptr;
        unsigned int tmp_buf_size = 0;
        int val = 0;

        tmp_ptr = buf;
        tmp_buf_size = *buf_size;
        for (i = 0; i < (tmp_buf_size - 2); i++){
            //check for 0x000003
            val = (tmp_ptr[i] ^ 0x00) + (tmp_ptr[i + 1] ^ 0x00) + (tmp_ptr[i + 2] ^ 0x03);
            if (val == 0){
                //kick out 0x03
                for (j = i + 2; j < tmp_buf_size - 1; j++)
                    tmp_ptr[j] = tmp_ptr[j + 1];
                //and so we should devrease bufsize
                (*buf_size)--;
            }
        }
        return;
    }
};

/**
    * 解码SPS,获取视频图像宽、高信息
    * @param buf SPS数据内容
    * @param nLen SPS数据的长度
    * @param width 图像宽度
    * @param height 图像高度
    *
    * 历史 Bug 记录(2026-07-23):
    *   现象: 当 H.264 编码为 High profile 且带 scaling matrix(如 h264-mpeg 编码)时,
    *         解析出的宽高错误(如 1920x1080 流被解析为 128x2144)。
    *   根因: seq_scaling_matrix_present_flag==1 时, 原代码只读取了 8 个
    *         seq_scaling_list_present_flag[i] 标志位, 但未调用 scaling_list 解析函数
    *         跳过 delta_scale 序列。delta_scale 序列占据大量 bit 位, 导致后续
    *         log2_max_frame_num / pic_order_cnt_type / num_ref_frames /
    *         pic_width_in_mbs_minus1 / pic_height_in_map_units_minus1 等 字段
    *         bit 位全部错位, 宽高计算错误。
    *   修复: 在 sps_parse namespace 新增 scaling_list() 函数(按 H.264 7.3.2.1.1 规范
    *         实现 lastScale/nextScale 循环), 在 seq_scaling_list_present_flag[i]==1
    *         时调用它, sizeOfScalingList 按 list 索引取 16(list 0-5) 或 8(list 6-7)。
    *   验证: 用 /tmp/sps.h264(实测流提取的 SPS+PPS)对照 ffprobe 解析,
    *         修复后结果 1920x1080、profile High(100)、level 4.2、1 参考帧, 与 ffprobe 一致。
    *   备注: 普通 Baseline profile 无 scaling matrix, 不会触发此 bug, 所以之前未暴露。
    */
bool h264_decode_sps(unsigned char* src_buf, unsigned int nLen, int& width, int& height, int& fps,int& max_ref_frames)
{
    UINT StartBit = 0;
    fps = 0;
    if(nLen>256){
        return false;
    }
    BYTE* buf = (BYTE*)malloc(nLen*sizeof(BYTE));
    if(!buf){
        return false;
    }
    memcpy(buf,src_buf,nLen);
    sps_parse::de_emulation_prevention(buf, &nLen);

    int forbidden_zero_bit = sps_parse::u(1, buf, StartBit);
    int nal_ref_idc = sps_parse::u(2, buf, StartBit);
    int nal_unit_type = sps_parse::u(5, buf, StartBit);
    if (nal_unit_type == 7){
        int profile_idc = sps_parse::u(8, buf, StartBit);
        int constraint_set0_flag = sps_parse::u(1, buf, StartBit);//(buf[1] & 0x80)>>7;
        int constraint_set1_flag = sps_parse::u(1, buf, StartBit);//(buf[1] & 0x40)>>6;
        int constraint_set2_flag = sps_parse::u(1, buf, StartBit);//(buf[1] & 0x20)>>5;
        int constraint_set3_flag = sps_parse::u(1, buf, StartBit);//(buf[1] & 0x10)>>4;
        int reserved_zero_4bits = sps_parse::u(4, buf, StartBit);
        int level_idc = sps_parse::u(8, buf, StartBit);

        int seq_parameter_set_id = sps_parse::Ue(buf, nLen, StartBit);

        if (profile_idc == 100 || profile_idc == 110 ||
            profile_idc == 122 || profile_idc == 144){
            int chroma_format_idc = sps_parse::Ue(buf, nLen, StartBit);
            if (chroma_format_idc == 3)
                int residual_colour_transform_flag = sps_parse::u(1, buf, StartBit);
            int bit_depth_luma_minus8 = sps_parse::Ue(buf, nLen, StartBit);
            int bit_depth_chroma_minus8 = sps_parse::Ue(buf, nLen, StartBit);
            int qpprime_y_zero_transform_bypass_flag = sps_parse::u(1, buf, StartBit);
            int seq_scaling_matrix_present_flag = sps_parse::u(1, buf, StartBit);

            int seq_scaling_list_present_flag[8];
            if (seq_scaling_matrix_present_flag){
                for (int i = 0; i < 8; i++) {
                    seq_scaling_list_present_flag[i] = sps_parse::u(1, buf, StartBit);
                    // 标志位为1时必须解析delta_scale序列,否则后续字段bit位错位
                    if (seq_scaling_list_present_flag[i]) {
                        // list 0-5 为16x16, list 6-7 为8x8
                        UINT sizeOfScalingList = (i < 6) ? 16 : 8;
                        sps_parse::scaling_list(buf, nLen, StartBit, sizeOfScalingList);
                    }
                }
            }
        }
        int log2_max_frame_num_minus4 = sps_parse::Ue(buf, nLen, StartBit);
        int pic_order_cnt_type = sps_parse::Ue(buf, nLen, StartBit);
        if (pic_order_cnt_type == 0)
            int log2_max_pic_order_cnt_lsb_minus4 = sps_parse::Ue(buf, nLen, StartBit);
        else if (pic_order_cnt_type == 1){
            int delta_pic_order_always_zero_flag = sps_parse::u(1, buf, StartBit);
            int offset_for_non_ref_pic = sps_parse::Se(buf, nLen, StartBit);
            int offset_for_top_to_bottom_field = sps_parse::Se(buf, nLen, StartBit);
            int num_ref_frames_in_pic_order_cnt_cycle = sps_parse::Ue(buf, nLen, StartBit);

            int* offset_for_ref_frame = new int[num_ref_frames_in_pic_order_cnt_cycle];
            for (int i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++)
                offset_for_ref_frame[i] = sps_parse::Se(buf, nLen, StartBit);
            delete[] offset_for_ref_frame;
        }
        int num_ref_frames = sps_parse::Ue(buf, nLen, StartBit);
        max_ref_frames = num_ref_frames;
        int gaps_in_frame_num_value_allowed_flag = sps_parse::u(1, buf, StartBit);
        int pic_width_in_mbs_minus1 = sps_parse::Ue(buf, nLen, StartBit);
        int pic_height_in_map_units_minus1 = sps_parse::Ue(buf, nLen, StartBit);

        //width=(pic_width_in_mbs_minus1+1)*16;
        //height=(pic_height_in_map_units_minus1+1)*16;

        int frame_mbs_only_flag = sps_parse::u(1, buf, StartBit);
        if (!frame_mbs_only_flag)
            int mb_adaptive_frame_field_flag = sps_parse::u(1, buf, StartBit);

        int direct_8x8_inference_flag = sps_parse::u(1, buf, StartBit);
        int frame_cropping_flag = sps_parse::u(1, buf, StartBit);
        int frame_crop_left_offset = 0;
        int frame_crop_right_offset = 0;
        int frame_crop_top_offset = 0;
        int frame_crop_bottom_offset = 0;
        if (frame_cropping_flag){
            frame_crop_left_offset = sps_parse::Ue(buf, nLen, StartBit);
            frame_crop_right_offset = sps_parse::Ue(buf, nLen, StartBit);
            frame_crop_top_offset = sps_parse::Ue(buf, nLen, StartBit);
            frame_crop_bottom_offset = sps_parse::Ue(buf, nLen, StartBit);
        }

        width = ((pic_width_in_mbs_minus1 + 1) * 16) - frame_crop_left_offset * 2 - frame_crop_right_offset * 2;
        height = ((2 - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16) - (frame_crop_top_offset * 2) - (frame_crop_bottom_offset * 2);

        //不需要获取fps了，就到这里返回吧
        free(buf);
        return true;

        // int vui_parameter_present_flag = sps_parse::u(1, buf, StartBit);
        // if (vui_parameter_present_flag){
        //     int aspect_ratio_info_present_flag = sps_parse::u(1, buf, StartBit);
        //     if (aspect_ratio_info_present_flag){
        //         int aspect_ratio_idc = sps_parse::u(8, buf, StartBit);
        //         if (aspect_ratio_idc == 255){
        //             int sar_width = sps_parse::u(16, buf, StartBit);
        //             int sar_height = sps_parse::u(16, buf, StartBit);
        //         }
        //     }
        //     int overscan_info_present_flag = sps_parse::u(1, buf, StartBit);
        //     if (overscan_info_present_flag)
        //         int overscan_appropriate_flagu = sps_parse::u(1, buf, StartBit);
        //     int video_signal_type_present_flag = sps_parse::u(1, buf, StartBit);
        //     if (video_signal_type_present_flag){
        //         int video_format = sps_parse::u(3, buf, StartBit);
        //         int video_full_range_flag = sps_parse::u(1, buf, StartBit);
        //         int colour_description_present_flag = sps_parse::u(1, buf, StartBit);
        //         if (colour_description_present_flag){
        //             int colour_primaries = sps_parse::u(8, buf, StartBit);
        //             int transfer_characteristics = sps_parse::u(8, buf, StartBit);
        //             int matrix_coefficients = sps_parse::u(8, buf, StartBit);
        //         }
        //     }
        //     int chroma_loc_info_present_flag = sps_parse::u(1, buf, StartBit);
        //     if (chroma_loc_info_present_flag){
        //         int chroma_sample_loc_type_top_field = sps_parse::Ue(buf, nLen, StartBit);
        //         int chroma_sample_loc_type_bottom_field = sps_parse::Ue(buf, nLen, StartBit);
        //     }
        //     int timing_info_present_flag = sps_parse::u(1, buf, StartBit);
        //     if (timing_info_present_flag){
        //         int num_units_in_tick = sps_parse::u(32, buf, StartBit);
        //         int time_scale = sps_parse::u(32, buf, StartBit);
        //         fps = time_scale / (2 * num_units_in_tick);
        //     }
        // }
        // free(buf);
        // return true;
    }
    else{
        free(buf);
    }
    return false;
}