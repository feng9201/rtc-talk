#pragma once

#include <QString>

#include <cstdint>
#include <optional>
#include <string>

namespace rtcwin::application {

struct StoredToken {
  std::string token;
  std::int64_t expires_at_ms = 0;
};

class TokenCredentialStore final {
 public:
  static QString TargetName(const std::string& signaling_url,
                            const std::string& uid,
                            const std::string& room_id);

  static std::optional<StoredToken> Load(const QString& target_name,
                                         QString* error = nullptr);
  static bool Store(const QString& target_name, const std::string& uid,
                    const std::string& token, std::int64_t expires_at_ms,
                    QString* error = nullptr);
  static bool Delete(const QString& target_name, QString* error = nullptr);

  static std::int64_t ParseJwtExpiryMs(const std::string& token);
};

}  // namespace rtcwin::application
