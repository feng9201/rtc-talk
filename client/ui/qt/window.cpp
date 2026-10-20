#include "window.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
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
#include <QSlider>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
TalkWindow::TalkWindow(){
  setWindowTitle(QStringLiteral("RTC Talk · RK3576"));resize(800,600);
  setStyleSheet("QPushButton,QLineEdit,QComboBox{min-height:34px;font-size:16px;} QLabel,QCheckBox{font-size:16px;}");
  auto* layout=new QVBoxLayout(this);auto* form=new QFormLayout;
  QSettings settings;
  url_=new QLineEdit(settings.value("url","ws://47.117.159.193:8080/talk/v1/ws").toString());
  uid_=new QLineEdit(settings.value("uid","rk3576-ui").toString());room_=new QLineEdit(settings.value("room","rk-client-test").toString());
  token_=new QLineEdit;token_->setEchoMode(QLineEdit::Password);token_->setPlaceholderText(QStringLiteral("仅保存在内存，不写配置"));
  callee_=new QLineEdit(settings.value("callee","peer-ui").toString());
  dev_=new QCheckBox(QStringLiteral("开发测试：从 /dev/token 获取凭证"));
  backend_=new QComboBox;backend_->addItem(QStringLiteral("仅信令（不访问声卡）"),"none");backend_->addItem(QStringLiteral("合成音频（不访问声卡）"),"fake");backend_->addItem("ALSA","alsa");backend_->addItem("PulseAudio","pulse");
  mode_=new QComboBox;mode_->addItem(QStringLiteral("自动（两人 P2P）"),"auto");mode_->addItem(QStringLiteral("仅 SFU"),"sfu");mode_->addItem(QStringLiteral("仅 P2P"),"p2p");
  mode_->setCurrentIndex(qMax(0,mode_->findData(settings.value("media_mode","auto").toString())));
  recording_=new QComboBox;playout_=new QComboBox;devices_=new QPushButton(QStringLiteral("枚举设备"));
  form->addRow(QStringLiteral("信令服务"),url_);form->addRow(QStringLiteral("用户标识"),uid_);form->addRow(QStringLiteral("房间 ID"),room_);form->addRow("Token",token_);form->addRow(dev_);form->addRow(QStringLiteral("音频后端"),backend_);
  form->addRow(QStringLiteral("呼叫对象"),callee_);form->addRow(QStringLiteral("媒体模式"),mode_);
  auto* audio=new QHBoxLayout;audio->addWidget(recording_);audio->addWidget(playout_);audio->addWidget(devices_);form->addRow(QStringLiteral("采集 / 播放"),audio);layout->addLayout(form);
  connect_=new QPushButton(QStringLiteral("连接"));disconnect_=new QPushButton(QStringLiteral("断开"));join_=new QPushButton(QStringLiteral("加入房间"));leave_=new QPushButton(QStringLiteral("离开"));mute_=new QPushButton(QStringLiteral("开麦"));
  auto* buttons=new QHBoxLayout;for(auto* b:{connect_,disconnect_,join_,leave_,mute_})buttons->addWidget(b);layout->addLayout(buttons);
  call_=new QPushButton(QStringLiteral("呼叫"));accept_=new QPushButton(QStringLiteral("接听"));hangup_=new QPushButton(QStringLiteral("挂断"));
  auto* callbar=new QHBoxLayout;callbar->addWidget(call_);callbar->addWidget(accept_);callbar->addWidget(hangup_);layout->addLayout(callbar);
  state_=new QLabel(QStringLiteral("未连接"));media_=new QLabel(QStringLiteral("媒体：未建立"));route_=new QLabel(QStringLiteral("媒体模式：SFU"));
  layout->addWidget(state_);layout->addWidget(media_);layout->addWidget(route_);
  members_=new QListWidget;layout->addWidget(members_);
  volume_=new QSlider(Qt::Horizontal);volume_->setRange(0,100);volume_->setValue(50);form->addRow(QStringLiteral("播放音量"),volume_);
  log_=new QPlainTextEdit;log_->setReadOnly(true);log_->setMaximumBlockCount(100);log_->setMaximumHeight(110);layout->addWidget(log_);
  http_=new QNetworkAccessManager(this);reset_buttons();
  connect(connect_,&QPushButton::clicked,this,&TalkWindow::connect_server);
  connect(disconnect_,&QPushButton::clicked,this,[this]{command("disconnect");});
  connect(join_,&QPushButton::clicked,this,[this]{join_->setEnabled(false);command("room.join",{{"room_id",room_->text()},{"display",uid_->text()}});});
  connect(leave_,&QPushButton::clicked,this,[this]{command("room.leave");});
  connect(mute_,&QPushButton::clicked,this,[this]{mute_->setEnabled(false);command("media.setMute",{{"muted",!muted_}});});
  connect(call_,&QPushButton::clicked,this,[this]{call_->setEnabled(false);command("call.invite",{{"callees",QJsonArray{callee_->text()}},{"room_id",room_->text()}});});
  connect(accept_,&QPushButton::clicked,this,[this]{accept_->setEnabled(false);if(call_id_.isEmpty())return;command("call.ringing",{{"call_id",call_id_}});command("call.accept",{{"call_id",call_id_}});});
  connect(hangup_,&QPushButton::clicked,this,[this]{hangup_->setEnabled(false);if(call_id_.isEmpty())return;command("call.hangup",{{"call_id",call_id_}});});
  connect(devices_,&QPushButton::clicked,this,[this]{if(create_core())command("audio.devices");});
  connect(volume_,&QSlider::sliderReleased,this,[this]{command("audio.volume",{{"gain",volume_->value()/100.0}});});
  auto* stats=new QTimer(this);stats->setInterval(10000);connect(stats,&QTimer::timeout,this,[this]{if(leave_->isEnabled()&&backend_->currentData().toString()!="none")command("media.stats");});stats->start();
}
TalkWindow::~TalkWindow(){
  QSettings s;s.setValue("url",url_->text());s.setValue("uid",uid_->text());s.setValue("room",room_->text());
  s.setValue("callee",callee_->text());s.setValue("media_mode",mode_->currentData().toString());
  if(core_)talk_destroy(core_);
}
void TalkWindow::reset_buttons(){
  connect_->setEnabled(true);disconnect_->setEnabled(false);join_->setEnabled(false);leave_->setEnabled(false);mute_->setEnabled(false);
  devices_->setEnabled(true);backend_->setEnabled(true);recording_->setEnabled(true);playout_->setEnabled(true);muted_=true;mute_->setText(QStringLiteral("开麦"));
  call_->setEnabled(false);accept_->setEnabled(false);hangup_->setEnabled(false);
  mode_->setEnabled(true);callee_->setEnabled(true);
}
bool TalkWindow::create_core(){
  ++generation_;
  if(core_){talk_destroy(core_);core_=nullptr;}
  context_=std::make_unique<CoreContext>(CoreContext{this,generation_});
  QJsonObject cfg{{"audio_backend",backend_->currentData().toString()},{"recording_device",recording_->currentData().toString()},{"playout_device",playout_->currentData().toString()},{"media_mode",mode_->currentData().toString()}};
  const auto config=QJsonDocument(cfg).toJson(QJsonDocument::Compact);
  talk_options o{sizeof(talk_options),TALK_ABI_VERSION,core_event,context_.get(),config.constData()};core_=talk_create(&o);
  if(!core_)log_->appendPlainText(QStringLiteral("创建核心库失败"));
  return core_;
}
void TalkWindow::command(const char* method,const QJsonObject& data){
  const auto text=QJsonDocument(data).toJson(QJsonDocument::Compact);
  if(!core_||!talk_command(core_,method,text.constData()))log_->appendPlainText(QStringLiteral("命令提交失败"));
}
void TalkWindow::enter_room(const QString& room){
  if(room.isEmpty()||leave_->isEnabled())return;
  room_->setText(room);join_->setEnabled(false);
  command("room.join",{{"room_id",room},{"display",uid_->text()}});
}
void TalkWindow::apply_mode(const QJsonObject& data){
  media_mode_=data.value("media_mode").toString(media_mode_);
  if(media_mode_!="p2p"){
    p2p_role_.clear();p2p_peer_.clear();p2p_path_.clear();
    route_->setText(QStringLiteral("媒体模式：SFU"));return;
  }
  const auto p=data.value("p2p").toObject();
  if(p.contains("role"))p2p_role_=p.value("role").toString();
  if(p.contains("peer"))p2p_peer_=p.value("peer").toString();
  QString text=QStringLiteral("媒体模式：P2P · %1 · 对端 %2").arg(p2p_role_,p2p_peer_);
  if(!p2p_path_.isEmpty())text+=QStringLiteral(" · 路径 %1").arg(p2p_path_);
  route_->setText(text);
}
void TalkWindow::update_call_buttons(){
  const bool online=disconnect_->isEnabled()&&!leave_->isEnabled();
  call_->setEnabled(online&&call_id_.isEmpty());
  accept_->setEnabled(online&&incoming_);
  hangup_->setEnabled(disconnect_->isEnabled()&&!call_id_.isEmpty());
}
void TalkWindow::connect_server(){
  const QUrl ws(url_->text());
  if(ws.scheme()!="ws"||ws.host().isEmpty()||ws.hasQuery()){log_->appendPlainText(QStringLiteral("测试版本要求不含 query 的 ws:// 地址"));return;}
  if(!create_core())return;
  connect_->setEnabled(false);devices_->setEnabled(false);backend_->setEnabled(false);recording_->setEnabled(false);playout_->setEnabled(false);
  mode_->setEnabled(false);callee_->setEnabled(false);
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
    if(d["state"]=="disconnected"){reset_buttons();peers_.clear();refresh_members();media_->setText(QStringLiteral("媒体：未建立"));
      media_mode_="sfu";call_id_.clear();call_room_.clear();incoming_=false;apply_mode(QJsonObject{{"media_mode","sfu"}});}
    else if(d["state"]=="reconnecting"){mute_->setEnabled(false);join_->setEnabled(false);muted_=true;mute_->setText(QStringLiteral("开麦"));update_call_buttons();}
  }else if(name=="ready"){self_uid_=d["uid"].toString();state_->setText(QStringLiteral("已连接"));disconnect_->setEnabled(true);join_->setEnabled(true);update_call_buttons();}
  else if(name=="joined"){
    peers_.clear();for(const auto p:d["peers"].toArray()){auto obj=p.toObject();peers_[obj["uid"].toString()]=obj;}refresh_members();
    join_->setEnabled(false);leave_->setEnabled(true);muted_=true;
    media_mode_="sfu";p2p_role_.clear();p2p_peer_.clear();p2p_path_.clear();apply_mode(d);
    if(backend_->currentData().toString()=="none")mute_->setEnabled(true);
    update_call_buttons();
  }else if(name=="published"){mute_->setEnabled(true);media_->setText(QStringLiteral("媒体：发布成功，默认静音"));}
  else if(name=="left"||name=="room.closed"){
    peers_.clear();refresh_members();join_->setEnabled(true);leave_->setEnabled(false);mute_->setEnabled(false);muted_=true;mute_->setText(QStringLiteral("开麦"));
    call_id_.clear();call_room_.clear();incoming_=false;apply_mode(QJsonObject{{"media_mode","sfu"}});update_call_buttons();
  }
  else if(name=="room.peerJoined"){peers_[d["uid"].toString()]=d;refresh_members();}
  else if(name=="room.peerLeft"){peers_.remove(d["uid"].toString());refresh_members();}
  else if(name=="room.peerUpdated"){auto id=d["uid"].toString();if(id==self_uid_)return;auto p=peers_.value(id);for(auto it=d.begin();it!=d.end();++it)p[it.key()]=it.value();peers_[id]=p;refresh_members();}
  else if(name=="media.forceMute"||name=="muted"){
    muted_=name=="media.forceMute"||d["muted"].toBool(true);
    mute_->setText(muted_?QStringLiteral("开麦"):QStringLiteral("静音"));
  }
  else if(name=="mediaMode"){apply_mode(d);state_->setText(QStringLiteral("媒体模式：%1").arg(media_mode_.toUpper()));}
  else if(name=="p2p.state"){
    const auto s=d.value("state").toString();
    if(s=="connected"){p2p_path_=d.value("path").toString(p2p_path_);apply_mode(QJsonObject{{"media_mode","p2p"}});}
    else if(s=="failed")route_->setText(QStringLiteral("媒体模式：P2P · 连接失败，等待服务端切换"));
  }
  else if(name=="p2p.path"){p2p_path_=d.value("path").toString(p2p_path_);apply_mode(QJsonObject{{"media_mode","p2p"}});}
  else if(name=="p2p.closed")p2p_path_.clear();
  else if(name=="call.incoming"){
    call_id_=d.value("call_id").toString();call_room_=d.value("room_id").toString();incoming_=true;
    callee_->setText(d.value("from").toString(callee_->text()));
    log_->appendPlainText(QStringLiteral("来电：%1 · 房间 %2").arg(d.value("from").toString(),call_room_));
    state_->setText(QStringLiteral("来电中"));update_call_buttons();
  }
  else if(name=="call.ringing")state_->setText(QStringLiteral("对方振铃中"));
  else if(name=="call.state"){
    const auto s=d.value("state").toString();
    if(!call_id_.isEmpty()&&d.value("call_id").toString()==call_id_){
      log_->appendPlainText(QStringLiteral("呼叫状态：%1").arg(s));
      if(s=="accepted"){incoming_=false;state_->setText(QStringLiteral("通话中 · %1").arg(media_mode_.toUpper()));}
      if(s=="rejected"||s=="cancelled"||s=="timeout"||s=="ended"){incoming_=false;call_id_.clear();call_room_.clear();}
      update_call_buttons();
    }
  }
  else if(name=="media.state")media_->setText(QStringLiteral("媒体 %1：状态 %2").arg(d["pc"].toString()).arg(d["state"].toInt()));
  else if(name=="result"){
    const auto method=d["method"].toString();const bool ok=d["ok"].toBool();
    if(!ok){log_->appendPlainText(QStringLiteral("%1 失败，错误码 %2").arg(method).arg(d["error"].toObject()["code"].toInt()));if(method=="room.join")join_->setEnabled(true);}
    if(method=="media.setMute"){
      mute_->setText(muted_?QStringLiteral("开麦"):QStringLiteral("静音"));mute_->setEnabled(leave_->isEnabled());
    }
    if(method=="audio.devices"&&ok){
      recording_->clear();playout_->clear();for(auto pair:{qMakePair(QString("recording"),recording_),qMakePair(QString("playout"),playout_)})
        for(auto v:d["data"].toObject()[pair.first].toArray()){auto device=v.toObject();pair.second->addItem(device["name"].toString(),device["id"].toString());}
    }
    if(method=="media.stats"&&ok)log_->appendPlainText(QString::fromUtf8(QJsonDocument(d["data"].toObject()).toJson(QJsonDocument::Compact)));
    if(method=="call.invite"&&ok){
      call_id_=d["data"].toObject().value("call_id").toString();call_room_=room_->text();incoming_=false;
      state_->setText(QStringLiteral("呼叫中"));enter_room(call_room_);
    }
    if(method=="call.accept"&&ok){incoming_=false;enter_room(call_room_);}
    if(method=="call.hangup"){call_id_.clear();call_room_.clear();incoming_=false;}
    if(method.startsWith("call."))update_call_buttons();
  }else if(name=="media.error"||name=="error")log_->appendPlainText(QStringLiteral("%1：%2").arg(name,d["msg"].toString()));
}
