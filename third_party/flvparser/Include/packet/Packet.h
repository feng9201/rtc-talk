//
// Created by 16037 on 2023-07-04.
//

#pragma once
#include <cstdint>
#include <memory>
namespace FlvParser{

    struct Packet {
        int64_t         pts{0};                 //时间戳
        uint8_t         *data{nullptr};         //数据
        uint32_t        size{0};                //数据大小
        int32_t         index{0};               //多媒体类型
        int32_t         codec_id{0};            //编码类型
        uint8_t         type{0};                //包类型  0: header  1: body 2 :end(eof)
        bool            key_frame{false};       //是否为关键帧
        uint64_t        track_id{ 0 };            //track id
        bool            has_track_id{ false };    //是否有track id

        ~Packet(){
            if(data){
                delete[] data;
                data = nullptr;
            }
        }
    };

}   // namespace FlvParser
