// File: core/base/json.h
// Brief: 统一 JSON 类型别名，避免各模块各自定义。
#ifndef TALK_BASE_JSON_H_
#define TALK_BASE_JSON_H_

#include <nlohmann/json.hpp>

namespace talk {

using Json = nlohmann::json;

}  // namespace talk

#endif  // TALK_BASE_JSON_H_
