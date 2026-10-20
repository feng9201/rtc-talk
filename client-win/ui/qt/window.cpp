#include "window.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHostInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
namespace {
// 首项固定为系统默认通信设备（空 id），其余为枚举结果。
void fill_devices(QComboBox* box,const QJsonArray& devices,const QString& selected){
  QSignalBlocker block(box);
  box->clear();
  box->addItem(QStringLiteral("系统默认（通信设备）"),QString());
  for(const auto& v:devices){const auto d=v.toObject();box->addItem(d["name"].toString(),d["id"].toString());}
  const int index=box->findData(selected);
  box->setCurrentIndex(index<0?0:index);
}
}
TalkWindow::TalkWindow(){
  setWindowTitle(QStringLiteral("RTC Talk · Windows"));resize(800,600);
  setStyleSheet("QPushButton,QLineEdit,QComboBox{min-height:34px;font-size:16px;} QLabel,QCheckBox{font-size:16px;}");
  auto* layout=new QVBoxLayout(this);auto* form=new QFormLayout;
  QSettings settings;
  url_=new QLineEdit(settings.value("url","ws://47.117.159.193:8080/talk/v1/ws").toString());
  uid_=new QLineEdit(settings.value("uid",QStringLiteral("windows-")+QHostInfo::localHostName().toLower()).toString());room_=new QLineEdit(settings.value("room","rk-client-test").toString());
  token_=new QLineEdit;token_->setEchoMode(QLineEdit::Password);token_->setPlaceholderText(QStringLiteral("仅保存在内存，不写配置"));
  dev_=new QCheckBox(QStringLiteral("开发测试：从 /dev/token 获取凭证"));
  backend_=new QComboBox;backend_->addItem(QStringLiteral("仅信令（不访问声卡）"),"none");backend_->addItem(QStringLiteral("合成音频（不访问声卡）"),"fake");backend_->addItem(QStringLiteral("WASAPI（Core Audio）"),"wasapi");
  {const int saved=backend_->findData(settings.value("backend","wasapi").toString());backend_->setCurrentIndex(saved<0?0:saved);}
  mode_=new QComboBox;mode_->addItem(QStringLiteral("自动（两人时 P2P）"),"auto");mode_->addItem(QStringLiteral("仅 SFU"),"sfu");mode_->addItem(QStringLiteral("强制 P2P"),"p2p");
  mode_->setToolTip(QStringLiteral("入房时作为策略提交；2 人且双方支持 P2P 时走直连"));
  {const int saved=mode_->findData(settings.value("media_mode","auto").toString());mode_->setCurrentIndex(saved<0?0:saved);}
  recording_=new QComboBox;playout_=new QComboBox;devices_=new QPushButton(QStringLiteral("枚举设备"));
  recording_->setMinimumWidth(200);playout_->setMinimumWidth(200);
  recording_->setToolTip(QStringLiteral("采集设备：默认使用系统通信设备"));playout_->setToolTip(QStringLiteral("播放设备：默认使用系统通信设备"));
  recording_id_=settings.value("recording_device").toString();playout_id_=settings.value("playout_device").toString();
  fill_devices(recording_,QJsonArray(),recording_id_);fill_devices(playout_,QJsonArray(),playout_id_);
  form->addRow(QStringLiteral("信令服务"),url_);form->addRow(QStringLiteral("用户标识"),uid_);form->addRow(QStringLiteral("房间 ID"),room_);form->addRow("Token",token_);form->addRow(dev_);form->addRow(QStringLiteral("音频后端"),backend_);form->addRow(QStringLiteral("媒体模式"),mode_);
  auto* audio=new QHBoxLayout;audio->addWidget(recording_);audio->addWidget(playout_);audio->addWidget(devices_);form->addRow(QStringLiteral("采集 / 播放"),audio);layout->addLayout(form);
  connect_=new QPushButton(QStringLiteral("连接"));disconnect_=new QPushButton(QStringLiteral("断开"));join_=new QPushButton(QStringLiteral("加入房间"));leave_=new QPushButton(QStringLiteral("离开"));mute_=new QPushButton(QStringLiteral("开麦"));
  auto* buttons=new QHBoxLayout;for(auto* b:{connect_,disconnect_,join_,leave_,mute_})buttons->addWidget(b);layout->addLayout(buttons);
  state_=new QLabel(QStringLiteral("未连接"));media_=new QLabel(QStringLiteral("媒体：未建立"));layout->addWidget(state_);layout->addWidget(media_);
  members_=new QListWidget;layout->addWidget(members_);
  volume_=new QSlider(Qt::Horizontal);volume_->setRange(0,100);volume_->setValue(50);form->addRow(QStringLiteral("播放音量"),volume_);
  log_=new QPlainTextEdit;log_->setReadOnly(true);log_->setMaximumBlockCount(100);log_->setMaximumHeight(110);layout->addWidget(log_);
  http_=new QNetworkAccessManager(this);reset_buttons();
  connect(connect_,&QPushButton::clicked,this,&TalkWindow::connect_server);
  connect(disconnect_,&QPushButton::clicked,this,[this]{command("disconnect");});
  connect(join_,&QPushButton::clicked,this,[this]{join_->setEnabled(false);command("room.join",{{"room_id",room_->text()},{"display",uid_->text()}});});
  connect(leave_,&QPushButton::clicked,this,[this]{command("room.leave");});
  connect(mute_,&QPushButton::clicked,this,[this]{mute_->setEnabled(false);command("media.setMute",{{"muted",!muted_}});});
  connect(devices_,&QPushButton::clicked,this,[this]{refresh_devices();});
  connect(backend_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{
    QSettings().setValue("backend",backend_->currentData().toString());
    reset_buttons();
    refresh_devices();
  });
  connect(mode_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{
    QSettings().setValue("media_mode",mode_->currentData().toString());
  });
  connect(recording_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{
    recording_id_=recording_->currentData().toString();QSettings().setValue("recording_device",recording_id_);
    if(connect_->isEnabled())log_->appendPlainText(QStringLiteral("采集设备已更新，下次连接生效"));
  });
  connect(playout_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{
    playout_id_=playout_->currentData().toString();QSettings().setValue("playout_device",playout_id_);
    if(connect_->isEnabled())log_->appendPlainText(QStringLiteral("播放设备已更新，下次连接生效"));
  });
  connect(volume_,&QSlider::sliderReleased,this,[this]{command("audio.volume",{{"gain",volume_->value()/100.0}});});
  auto* stats=new QTimer(this);stats->setInterval(10000);connect(stats,&QTimer::timeout,this,[this]{if(leave_->isEnabled()&&backend_->currentData().toString()!="none")command("media.stats");});stats->start();
  QTimer::singleShot(0,this,[this]{refresh_devices();});
}
TalkWindow::~TalkWindow(){
  QSettings s;s.setValue("url",url_->text());s.setValue("uid",uid_->text());s.setValue("room",room_->text());
  s.setValue("backend",backend_->currentData().toString());
  s.setValue("media_mode",mode_->currentData().toString());
  s.setValue("recording_device",recording_->currentData().toString());s.setValue("playout_device",playout_->currentData().toString());
  if(core_)talk_destroy(core_);
}
void TalkWindow::reset_buttons(){
  connect_->setEnabled(true);disconnect_->setEnabled(false);join_->setEnabled(false);leave_->setEnabled(false);mute_->setEnabled(false);
  devices_->setEnabled(true);backend_->setEnabled(true);mode_->setEnabled(true);
  const bool audio=backend_->currentData().toString()=="wasapi";
  recording_->setEnabled(audio);playout_->setEnabled(audio);muted_=true;mute_->setText(QStringLiteral("开麦"));
}
void TalkWindow::refresh_devices(){
  if(!connect_->isEnabled())return;  // 连接或通话中不改动核心库
  const bool audio=backend_->currentData().toString()=="wasapi";
  recording_->setEnabled(audio);playout_->setEnabled(audio);
  if(!audio){fill_devices(recording_,QJsonArray(),recording_id_);fill_devices(playout_,QJsonArray(),playout_id_);return;}
  if(!create_core())return;
  command("audio.devices");
}
bool TalkWindow::create_core(){
  ++generation_;
  if(core_){talk_destroy(core_);core_=nullptr;}
  context_=std::make_unique<CoreContext>(CoreContext{this,generation_});
  QJsonObject cfg{{"audio_backend",backend_->currentData().toString()},{"media_mode",mode_->currentData().toString()},{"recording_device",recording_->currentData().toString()},{"playout_device",playout_->currentData().toString()}};
  const auto config=QJsonDocument(cfg).toJson(QJsonDocument::Compact);
  talk_options o{sizeof(talk_options),TALK_ABI_VERSION,core_event,context_.get(),config.constData()};core_=talk_create(&o);
  if(!core_)log_->appendPlainText(QStringLiteral("创建核心库失败"));
  return core_;
}
void TalkWindow::command(const char* method,const QJsonObject& data){
  const auto text=QJsonDocument(data).toJson(QJsonDocument::Compact);
  if(!core_||!talk_command(core_,method,text.constData()))log_->appendPlainText(QStringLiteral("命令提交失败"));
}
void TalkWindow::connect_server(){
  const QUrl ws(url_->text());
  if(ws.scheme()!="ws"||ws.host().isEmpty()||ws.hasQuery()){log_->appendPlainText(QStringLiteral("测试版本要求不含 query 的 ws:// 地址"));return;}
  if(!create_core())return;
  connect_->setEnabled(false);devices_->setEnabled(false);backend_->setEnabled(false);mode_->setEnabled(false);recording_->setEnabled(false);playout_->setEnabled(false);
  if(!dev_->isChecked()){
    if(token_->text().isEmpty()){log_->appendPlainText(QStringLiteral("请填写 Token 或显式启用开发模式"));reset_buttons();return;}
    command("connect",{{"url",url_->text()},{"token",token_->text()}});return;
  }
  QUrl endpoint=ws;endpoint.setScheme("http");endpoint.setPath("/dev/token");QUrlQuery query;query.addQueryItem("uid",uid_->text());query.addQueryItem("rooms",room_->text());endpoint.setQuery(query);
  auto* reply=http_->get(QNetworkRequest(endpoint));QTimer::singleShot(8000,reply,[reply]{if(reply->isRunning())reply->abort();});
  connect(reply,&QNetworkReply::finished,this,[this,reply]{
    const auto data=QJsonDocument::fromJson(reply->readAll()).object();
    if(reply->error()!=QNetworkReply::NoError||data["token"].toString().isEmpty()){log_->appendPlainText(QStringLiteral("开发凭证获取失败"));reset_buttons();}
    else command("connect",{{"url",url_->text()},{"token",data["token"].toString()}});
    reply->deleteLater();
  });
}
void TalkWindow::core_event(void* context,const char* event,const char* text){
  const auto c=*static_cast<CoreContext*>(context);
  const auto name=QString::fromUtf8(event);const auto data=QJsonDocument::fromJson(QByteArray(text)).object();
  QMetaObject::invokeMethod(c.window,[c,name,data]{if(c.generation==c.window->generation_)c.window->receive(name,data);},Qt::QueuedConnection);
}
void TalkWindow::refresh_members(){
  members_->clear();for(auto it=peers_.begin();it!=peers_.end();++it){auto p=it.value();members_->addItem(QStringLiteral("%1 · %2 · %3").arg(p["display"].toString(it.key()),p["role"].toString(),p["audio"].toObject()["muted"].toBool()?QStringLiteral("静音"):QStringLiteral("开麦")));}
}
void TalkWindow::receive(const QString& name,const QJsonObject& d){
  if(name=="state"){
    state_->setText(QStringLiteral("信令：%1").arg(d["state"].toString()));
    if(d["state"]=="disconnected"){reset_buttons();peers_.clear();refresh_members();media_->setText(QStringLiteral("媒体：未建立"));}
    else if(d["state"]=="reconnecting"){mute_->setEnabled(false);join_->setEnabled(false);muted_=true;mute_->setText(QStringLiteral("开麦"));}
  }else if(name=="ready"){self_uid_=d["uid"].toString();state_->setText(QStringLiteral("已连接"));disconnect_->setEnabled(true);join_->setEnabled(true);}
  else if(name=="joined"){
    peers_.clear();for(const auto p:d["peers"].toArray()){auto obj=p.toObject();peers_[obj["uid"].toString()]=obj;}refresh_members();
    join_->setEnabled(false);leave_->setEnabled(true);muted_=true;
    const auto media_mode=d["media_mode"].toString(QStringLiteral("sfu"));
    if(media_mode=="p2p"){
      const auto p2p=d["p2p"].toObject();
      media_->setText(QStringLiteral("P2P 协商中：%1（%2）").arg(p2p["peer"].toString()).arg(p2p["role"].toString()));
    }else media_->setText(QStringLiteral("SFU：发布中"));
    if(backend_->currentData().toString()=="none")mute_->setEnabled(true);
  }else if(name=="published"){
    mute_->setEnabled(true);
    media_->setText(d["mode"].toString()=="p2p"?QStringLiteral("媒体：P2P 已连接，默认静音"):QStringLiteral("媒体：发布成功，默认静音"));
  }else if(name=="p2p.started"){
    media_->setText(QStringLiteral("P2P 协商中（%1）").arg(d["role"].toString()));
    log_->appendPlainText(QStringLiteral("P2P 会话 %1 已启动，对端 %2").arg(d["session"].toString(),d["peer"].toString()));
  }else if(name=="p2p.state"){
    const auto state=d["state"].toString();
    media_->setText(state=="connected"?QStringLiteral("媒体：P2P 已连接（%1）").arg(d["path"].toString()):QStringLiteral("媒体：P2P %1").arg(state));
    log_->appendPlainText(QStringLiteral("P2P 状态：%1 %2").arg(state,d["path"].toString()));
  }
  else if(name=="left"||name=="room.closed"){peers_.clear();refresh_members();join_->setEnabled(true);leave_->setEnabled(false);mute_->setEnabled(false);muted_=true;mute_->setText(QStringLiteral("开麦"));}
  else if(name=="room.peerJoined"){peers_[d["uid"].toString()]=d;refresh_members();}
  else if(name=="room.peerLeft"){peers_.remove(d["uid"].toString());refresh_members();}
  else if(name=="room.peerUpdated"){auto id=d["uid"].toString();if(id==self_uid_)return;auto p=peers_.value(id);for(auto it=d.begin();it!=d.end();++it)p[it.key()]=it.value();peers_[id]=p;refresh_members();}
  else if(name=="media.forceMute"||name=="muted"){
    muted_=name=="media.forceMute"||d["muted"].toBool(true);
    mute_->setText(muted_?QStringLiteral("开麦"):QStringLiteral("静音"));
  }
  else if(name=="media.state")media_->setText(QStringLiteral("媒体 %1：状态 %2").arg(d["pc"].toString()).arg(d["state"].toInt()));
  else if(name=="result"){
    const auto method=d["method"].toString();const bool ok=d["ok"].toBool();
    if(!ok){log_->appendPlainText(QStringLiteral("%1 失败，错误码 %2").arg(method).arg(d["error"].toObject()["code"].toInt()));if(method=="room.join")join_->setEnabled(true);}
    if(method=="media.setMute"){
      mute_->setText(muted_?QStringLiteral("开麦"):QStringLiteral("静音"));mute_->setEnabled(leave_->isEnabled());
    }
    if(method=="audio.devices"&&ok){
      const auto list=d["data"].toObject();
      fill_devices(recording_,list["recording"].toArray(),recording_id_);
      fill_devices(playout_,list["playout"].toArray(),playout_id_);
      log_->appendPlainText(QStringLiteral("音频设备：采集 %1 个，播放 %2 个").arg(list["recording"].toArray().size()).arg(list["playout"].toArray().size()));
    }
    if(method=="media.stats"&&ok)log_->appendPlainText(QString::fromUtf8(QJsonDocument(d["data"].toObject()).toJson(QJsonDocument::Compact)));
  }else if(name=="media.error"||name=="error")log_->appendPlainText(QStringLiteral("%1：%2").arg(name,d["msg"].toString()));
}
