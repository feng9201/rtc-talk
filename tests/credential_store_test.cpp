#include "rtcwin/application/token_credential_store.h"

#include <QDateTime>

#include <iostream>

int main() {
  using rtcwin::application::TokenCredentialStore;
  const std::int64_t jwt_expiry = TokenCredentialStore::ParseJwtExpiryMs(
      "e30.eyJleHAiOjIwMDAwMDAwMDB9.signature");
  if (jwt_expiry != 2000000000000LL) {
    std::cerr << "JWT expiry parse mismatch: " << jwt_expiry << '\n';
    return 1;
  }
  const QString target = TokenCredentialStore::TargetName(
      "wss://unit.test/talk/v1/ws", "credential-test", "credential-room");
  QString error;
  if (!TokenCredentialStore::Delete(target, &error)) {
    std::cerr << "delete before test failed: "
              << error.toStdString() << '\n';
    return 1;
  }
  const std::int64_t future =
      QDateTime::currentMSecsSinceEpoch() + 10 * 60 * 1000;
  if (!TokenCredentialStore::Store(target, "credential-test",
                                   "test-token-value", future, &error)) {
    std::cerr << "store failed: " << error.toStdString() << '\n';
    return 2;
  }
  const auto loaded = TokenCredentialStore::Load(target, &error);
  if (!loaded || loaded->token != "test-token-value" ||
      loaded->expires_at_ms != future) {
    std::cerr << "load mismatch: " << error.toStdString() << '\n';
    return 3;
  }
  const std::int64_t expired = QDateTime::currentMSecsSinceEpoch() - 1000;
  if (!TokenCredentialStore::Store(target, "credential-test",
                                   "expired-token", expired, &error)) {
    std::cerr << "store expired failed: " << error.toStdString() << '\n';
    return 4;
  }
  if (TokenCredentialStore::Load(target, &error)) {
    std::cerr << "expired credential was returned\n";
    return 5;
  }
  if (!TokenCredentialStore::Delete(target, &error)) {
    std::cerr << "final delete failed: " << error.toStdString() << '\n';
    return 6;
  }
  std::cout << "credential store test: PASS\n";
  return 0;
}
