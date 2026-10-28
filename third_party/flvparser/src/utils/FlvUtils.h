//
// Created by 16037 on 2023-07-04.
//

#pragma once
#include <cstdint>
#include <string>

namespace FlvParser{

    union endian{
        uint32_t i;
        char c[4];
    };

    static endian e = {1};
    static bool isBigEndian = e.c[0] == 0;


    struct FlvUtils {

        template <class T>
        static T valueFromBtye(char* in, const size_t length_ = -1) noexcept(false) {

            size_t length = (length_ == -1 ? sizeof(T) : length_);

            char* bytes = in;
            T sum = 0;
            for (int i = 0; i < length; ++i) {
                if(isBigEndian)
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
                if(isBigEndian)
                    *(bytes + i) = (value >> (8 * i)) & 0xff;
                else
                    *(bytes + i) = (value >> (8 * (length - i - 1))) & 0xff;
            }
        }


        static std::string url_scheme(const std::string& url);

        static std::string url_authority(const std::string& url);

        static std::string url_path(const std::string& url);
    };

} //namespace FlvParser
