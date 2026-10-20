#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>
#include <QTimer>
#include <QPixmap>
#include <QScreen>
#include <QDebug>
int main(int argc,char** argv){
  QApplication app(argc,argv);app.setOrganizationName("rtc-talk");app.setApplicationName("talk-qt");
  QCommandLineParser parser;parser.addHelpOption();
  parser.addOption({"smoke-seconds",QStringLiteral("无连接界面冒烟测试后自动退出"),"seconds"});
  parser.addOption({"screenshot",QStringLiteral("冒烟测试时保存本窗口截图"),"path"});
  parser.addOption({"backend",QStringLiteral("本次启动覆盖音频后端（写入配置后由界面生效）"),"name"});parser.process(app);
  if(parser.isSet("backend")){
    const auto backend=parser.value("backend");
    if(backend!="none"&&backend!="fake"&&backend!="wasapi")return 2;
    QSettings().setValue("backend",backend);
  }
  TalkWindow window;window.show();
  if(parser.isSet("smoke-seconds")){
    bool ok=false;int seconds=parser.value("smoke-seconds").toInt(&ok);if(!ok||seconds<1||seconds>60)return 2;
    QTimer::singleShot(500,&window,[&]{
      for(auto* screen:app.screens())qInfo()<<"screen"<<screen->name()<<screen->geometry();
      if(parser.isSet("screenshot")&&!window.grab().save(parser.value("screenshot")))app.exit(3);
    });
    QTimer::singleShot(seconds*1000,&app,&QApplication::quit);
  }
  return app.exec();
}
