// File: ui/qt/core_client.h
// Brief: 封装 talk.h C ABI：句柄生命周期、事件回投 UI 线程与命令提交。
#ifndef TALK_UI_CORE_CLIENT_H_
#define TALK_UI_CORE_CLIENT_H_

#include <QJsonObject>
#include <QObject>
#include <cstdint>
#include <memory>

#include "talk/talk.h"

// 每个实例持有一个 talk_handle；Start 会替换旧句柄并丢弃旧事件。
class CoreClient : public QObject {
  Q_OBJECT

 public:
  explicit CoreClient(QObject* parent = nullptr);
  ~CoreClient() override;

  CoreClient(const CoreClient&) = delete;
  CoreClient& operator=(const CoreClient&) = delete;

  // 按配置创建核心库；失败返回 false。
  bool Start(const QJsonObject& config);
  bool Submit(const QString& method,
              const QJsonObject& data = QJsonObject());

 signals:
  // 在 UI 线程发出；旧句柄的事件不会到达。
  void Event(const QString& name, const QJsonObject& data);

 private:
  struct Context {
    CoreClient* self = nullptr;
    uint64_t generation = 0;
  };

  static void Dispatch(void* context, const char* name, const char* text);
  void Deliver(const QString& name, const QJsonObject& data);

  std::unique_ptr<Context> context_;
  talk_handle* core_ = nullptr;
  uint64_t generation_ = 0;
};

#endif  // TALK_UI_CORE_CLIENT_H_
