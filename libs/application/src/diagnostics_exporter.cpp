#include "rtcwin/application/diagnostics_exporter.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <private/qzipwriter_p.h>

namespace rtcwin::application {
namespace {

QString SafeId(const std::string& value) {
  if (value.empty()) return {};
  return QString::fromLatin1(
      QCryptographicHash::hash(QByteArray::fromStdString(value),
                               QCryptographicHash::Sha256)
          .toHex()
          .left(12));
}

QString RedactLogs(const QByteArray& input) {
  QString result;
  const QString text = QString::fromUtf8(input);
  for (const QString& line : text.split(QLatin1Char('\n'))) {
    const QString lower = line.toLower();
    if (lower.contains(QStringLiteral("token=")) ||
        lower.contains(QStringLiteral("authorization")) ||
        lower.contains(QStringLiteral("credential=")) ||
        lower.contains(QStringLiteral("password=")) ||
        lower.contains(QStringLiteral("\"sdp\"")) ||
        lower.contains(QStringLiteral("candidate:"))) {
      result += QStringLiteral("[redacted sensitive log line]\n");
    } else {
      result += line;
      result += QLatin1Char('\n');
    }
  }
  return result;
}

QJsonObject SessionJson(const domain::SessionSnapshot& snapshot) {
  QJsonObject root;
  root.insert(QStringLiteral("schema_version"), 1);
  root.insert(QStringLiteral("room_id"), QString::fromStdString(snapshot.room_id));
  root.insert(QStringLiteral("self_uid_hash"), SafeId(snapshot.self_uid));
  root.insert(QStringLiteral("self_role"),
              snapshot.self_role == domain::RoomRole::Host
                  ? QStringLiteral("host")
                  : QStringLiteral("member"));
  root.insert(QStringLiteral("talk_mode"),
              QString::fromStdString(domain::TalkModeName(snapshot.talk_mode)));
  root.insert(QStringLiteral("media_mode"),
              QString::fromStdString(snapshot.media_mode));
  root.insert(QStringLiteral("connection_phase"),
              static_cast<int>(snapshot.connection));
  root.insert(QStringLiteral("join_phase"), static_cast<int>(snapshot.join));
  root.insert(QStringLiteral("mic_muted"), snapshot.mic_muted);
  root.insert(QStringLiteral("camera_on"), snapshot.camera_on);
  root.insert(QStringLiteral("sharing"), snapshot.sharing);
  root.insert(QStringLiteral("deafened"), snapshot.deafened);
  root.insert(QStringLiteral("policy"),
              QJsonObject{{QStringLiteral("talk_mode"),
                           QString::fromStdString(
                               domain::TalkModeName(snapshot.policy.talk_mode))},
                          {QStringLiteral("max_speakers"),
                           snapshot.policy.max_speakers},
                          {QStringLiteral("ptt_max_hold_sec"),
                           snapshot.policy.ptt_max_hold_seconds},
                          {QStringLiteral("ptt_queue"),
                           snapshot.policy.ptt_queue},
                          {QStringLiteral("max_peers"),
                           snapshot.policy.max_peers},
                          {QStringLiteral("video_enabled"),
                           snapshot.policy.video_enabled},
                          {QStringLiteral("video_max_publishers"),
                           snapshot.policy.video_max_publishers}});

  QJsonArray peers;
  for (const domain::PeerState& peer : snapshot.peers) {
    peers.push_back(QJsonObject{
        {QStringLiteral("uid_hash"), SafeId(peer.uid)},
        {QStringLiteral("display"),
         QString::fromStdString(peer.display_name)},
        {QStringLiteral("role"),
         peer.role == domain::RoomRole::Host ? QStringLiteral("host")
                                            : QStringLiteral("member")},
        {QStringLiteral("muted"), peer.audio_muted},
        {QStringLiteral("speaking"), peer.speaking},
        {QStringLiteral("camera_on"), peer.camera_on},
        {QStringLiteral("screen_on"), peer.screen_on},
        {QStringLiteral("audio_level"), peer.audio_level}});
  }
  root.insert(QStringLiteral("peers"), peers);
  root.insert(QStringLiteral("devices"),
              QJsonObject{
                  {QStringLiteral("recording_configured"),
                   !snapshot.devices.recording_device.empty()},
                  {QStringLiteral("playout_configured"),
                   !snapshot.devices.playout_device.empty()},
                  {QStringLiteral("system_audio_configured"),
                   !snapshot.devices.system_audio_device.empty()},
                  {QStringLiteral("camera_configured"),
                   !snapshot.devices.camera_device.empty()},
                  {QStringLiteral("screen_source_configured"),
                   !snapshot.devices.screen_source.empty()}});
  root.insert(QStringLiteral("media_stats"),
              QJsonObject{
                  {QStringLiteral("available"),
                   snapshot.media_stats.available},
                  {QStringLiteral("packets_lost"),
                   static_cast<qint64>(snapshot.media_stats.packets_lost)},
                  {QStringLiteral("rtt_ms"), snapshot.media_stats.rtt_ms},
                  {QStringLiteral("jitter_ms"),
                   snapshot.media_stats.jitter_ms},
                  {QStringLiteral("loss_percent"),
                   snapshot.media_stats.loss_percent},
                  {QStringLiteral("bitrate_bps"),
                   snapshot.media_stats.bitrate_bps},
                  {QStringLiteral("available_outgoing_bitrate_bps"),
                   snapshot.media_stats.available_outgoing_bitrate_bps},
                  {QStringLiteral("candidate_pair_redacted"),
                   snapshot.media_stats.candidate_pair.empty()
                       ? QStringLiteral("none")
                       : QStringLiteral("<redacted>")}});
  return root;
}

}  // namespace

bool DiagnosticsExporter::Export(const domain::SessionSnapshot& snapshot,
                                 const QString& log_path,
                                 const QString& output_directory,
                                 QString* output_zip,
                                 QString* error) {
  if (output_zip == nullptr) {
    if (error) *error = QStringLiteral("诊断包输出路径为空");
    return false;
  }
  QDir directory(output_directory);
  if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
    if (error) *error = QStringLiteral("无法创建诊断包目录");
    return false;
  }
  const QString stamp =
      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  const QString path =
      directory.filePath(QStringLiteral("rtc-talk-diagnostics-%1.zip").arg(stamp));
  QZipWriter zip(path);
  if (!zip.isWritable()) {
    if (error) *error = QStringLiteral("无法创建诊断 ZIP");
    return false;
  }

  const QJsonObject session = SessionJson(snapshot);
  const QByteArray session_bytes =
      QJsonDocument(session).toJson(QJsonDocument::Indented);
  zip.addFile(QStringLiteral("session.json"), session_bytes);

  QByteArray log_bytes;
  const QFileInfo log_info(log_path);
  if (log_info.exists() && log_info.isFile() &&
      log_info.size() <= 5 * 1024 * 1024) {
    QFile log(log_path);
    if (log.open(QIODevice::ReadOnly)) log_bytes = log.readAll();
  }
  zip.addFile(QStringLiteral("logs/rtcwin.log"),
              RedactLogs(log_bytes).toUtf8());

  const QJsonObject manifest{
      {QStringLiteral("schema_version"), 1},
      {QStringLiteral("generated_at"),
       QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
      {QStringLiteral("session_file"), QStringLiteral("session.json")},
      {QStringLiteral("log_file"), QStringLiteral("logs/rtcwin.log")},
      {QStringLiteral("redaction"),
       QStringLiteral("token/credential/password/SDP/candidate and endpoint IDs")}};
  zip.addFile(QStringLiteral("manifest.json"),
              QJsonDocument(manifest).toJson(QJsonDocument::Indented));
  zip.addFile(
      QStringLiteral("README.txt"),
      QStringLiteral(
          "rtc-talk-win 诊断包\n"
          "包含会话快照、脱敏媒体统计和脱敏 Engine 日志。\n"
          "不包含 Token、TURN 密码、SDP、ICE candidate 或原始设备端点 ID。\n")
          .toUtf8());
  zip.close();
  if (zip.status() != QZipWriter::NoError) {
    if (error) *error = QStringLiteral("写入诊断 ZIP 失败");
    return false;
  }
  *output_zip = path;
  return true;
}

}  // namespace rtcwin::application
