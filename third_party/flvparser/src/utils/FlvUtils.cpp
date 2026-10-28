//
// Created by 16037 on 2023-07-04.
//

#include "FlvUtils.h"

using namespace FlvParser;

std::string FlvUtils::url_scheme(const std::string &url) {
    std::string scheme;
    size_t pos = url.find("://");
    if(pos != std::string::npos){
        scheme = url.substr(0,pos);
    }
    return scheme;
}

std::string FlvUtils::url_authority(const std::string &url) {
    std::string authority;
    size_t pos = url.find("://");
    if(pos != std::string::npos){
        authority = url.substr(pos+3);
        pos = authority.find("/");
        if(pos != std::string::npos){
            authority = authority.substr(0,pos);
        }
    }
    return authority;
}

std::string FlvUtils::url_path(const std::string &url) {
    std::string path;
    size_t pos = url.find("://");
    if(pos != std::string::npos){
        path = url.substr(pos+3);
        pos = path.find("/");
        if(pos != std::string::npos){
            path = path.substr(pos);
        }
    }
    return path;
}
