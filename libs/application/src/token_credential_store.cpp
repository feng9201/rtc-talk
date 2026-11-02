#include "rtcwin/application/token_credential_store.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include <windows.h>
#include <wincred.h>

#include <algorithm>
#include <vector>

namespace rtcwin::application {
namespace {

QString WindowsError(const QString& operation, DWORD code) {
  return QStringLiteral("%1 failed (Windows error %2)")
      .arg(operation)
      .arg(static_cast<qulonglong>(code));
}

std::string DecodeBase64Url(const QString& text) {
  QString normalized = text;
  normalized.replace('-', '+');
  normalized.replace('_', '/');
  while ((normalized.size() % 4) != 0) normalized += QLatin1Char('=');
  const QByteArray decoded =
      QByteArray::fromBase64(normalized.toLatin1(),
                             QByteArray::Base64Encoding |
                                 QByteArray::OmitTrailingEquals);
  return decoded.toStdString();
}

}  // namespace

QString TokenCredentialStore::TargetName(const std::string& signaling_url,
                                         const std::string& uid,
                                         const std::string& room_id) {
  const QString normalized_url =
      QString::fromStdString(signaling_url).trimmed().toLower();
  QByteArray identity = normalized_url.toUtf8() + '\n' +
             QByteArray::fromStdString(uid) + '\n' +
             QByteArray::fromStdString(room_id);
  const QByteArray digest =
      QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
  return QStringLiteral("rtc-talk-win/token/") +
         QString::fromLatin1(digest);
}

std::int64_t TokenCredentialStore::ParseJwtExpiryMs(
    const std::string& token) {
  const QString text = QString::fromStdString(token);
  const QStringList parts = text.split(QLatin1Char('.'));
  if (parts.size() != 3) return 0;
  const std::string payload = DecodeBase64Url(parts.at(1));
  const QJsonDocument document = QJsonDocument::fromJson(
      QByteArray::fromStdString(payload));
  if (!document.isObject()) return 0;
  const QJsonValue exp = document.object().value(QStringLiteral("exp"));
  if (!exp.isDouble() || exp.toDouble() <= 0) return 0;
  return static_cast<std::int64_t>(exp.toDouble() * 1000.0);
}

std::optional<StoredToken> TokenCredentialStore::Load(
    const QString& target_name, QString* error) {
  PCREDENTIALW credential = nullptr;
  const std::wstring target = target_name.toStdWString();
  if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
    const DWORD code = GetLastError();
    if (code == ERROR_NOT_FOUND) return std::nullopt;
    if (error) *error = WindowsError(QStringLiteral("CredReadW"), code);
    return std::nullopt;
  }

  const QByteArray blob(
      reinterpret_cast<const char*>(credential->CredentialBlob),
      static_cast<int>(credential->CredentialBlobSize));
  CredFree(credential);

  const QJsonObject object = QJsonDocument::fromJson(blob).object();
  StoredToken result;
  result.token = object.value(QStringLiteral("token")).toString().toStdString();
  result.expires_at_ms = static_cast<std::int64_t>(
      object.value(QStringLiteral("expires_at_ms")).toDouble(0));
  if (result.token.empty()) {
    if (error) *error = QStringLiteral("Credential Manager token is empty");
    Delete(target_name);
    return std::nullopt;
  }
  if (result.expires_at_ms > 0 &&
      result.expires_at_ms <= QDateTime::currentMSecsSinceEpoch() + 30000) {
    Delete(target_name);
    return std::nullopt;
  }
  return result;
}

bool TokenCredentialStore::Store(const QString& target_name,
                                 const std::string& uid,
                                 const std::string& token,
                                 std::int64_t expires_at_ms,
                                 QString* error) {
  if (target_name.isEmpty() || uid.empty() || token.empty()) {
    if (error) *error = QStringLiteral("Credential Manager input is empty");
    return false;
  }
  const std::int64_t parsed_expiry = ParseJwtExpiryMs(token);
  if (expires_at_ms <= 0) expires_at_ms = parsed_expiry;

  const QJsonObject object{
      {QStringLiteral("token"), QString::fromStdString(token)},
      {QStringLiteral("expires_at_ms"),
       static_cast<double>(expires_at_ms)}};
  const QByteArray blob = QJsonDocument(object).toJson(QJsonDocument::Compact);
  if (blob.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
    if (error) *error = QStringLiteral("Token exceeds Credential Manager limit");
    return false;
  }

  std::wstring target = target_name.toStdWString();
  std::wstring username = QString::fromStdString(uid).toStdWString();
  CREDENTIALW credential{};
  credential.Type = CRED_TYPE_GENERIC;
  credential.TargetName = target.data();
  credential.UserName = username.data();
  credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
  credential.CredentialBlob =
      reinterpret_cast<LPBYTE>(const_cast<char*>(blob.constData()));
  credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
  if (!CredWriteW(&credential, 0)) {
    if (error) {
      *error = WindowsError(QStringLiteral("CredWriteW"), GetLastError());
    }
    return false;
  }
  return true;
}

bool TokenCredentialStore::Delete(const QString& target_name, QString* error) {
  if (target_name.isEmpty()) return true;
  const std::wstring target = target_name.toStdWString();
  if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) return true;
  const DWORD code = GetLastError();
  if (code == ERROR_NOT_FOUND) return true;
  if (error) *error = WindowsError(QStringLiteral("CredDeleteW"), code);
  return false;
}

}  // namespace rtcwin::application
