#include "apptranslator.h"
#include "manager.h"
#include "singleapplication.h"
#include "widgetstate.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLocale>

#include <locale.h>

#define STR2(XXX) #XXX
#define STR(XXX) STR2(XXX)

int main(int argc, char *argv[])
{
  QLocale::setDefault(QLocale(QLocale::Russian, QLocale::Russia));
#ifdef Q_OS_LINUX
  qunsetenv("QTWEBENGINE_REMOTE_DEBUGGING");
  if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
    qputenv("QTWEBENGINE_DISABLE_GPU", "1");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            "--disable-gpu --disable-gpu-compositing --disable-software-rasterizer --disable-logging");
  }
#endif

  QApplication a(argc, argv);
  a.setApplicationName("WaylandScreenTranslator");
  a.setOrganizationName("sithond");
  a.setApplicationVersion(STR(VERSION));

  a.setQuitOnLastWindowClosed(false);

  {
    service::AppTranslator appTranslator({"screentranslator"});
    appTranslator.retranslate();
  }

  {
    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("OCR and translation tool"));
    parser.addHelpOption();
    parser.addVersionOption();
    service::WidgetState::addHelp(parser);

    parser.process(a);
  }

  service::SingleApplication guard;
  if (!guard.isValid())
    return 1;

  // tesseract recommends
  setlocale(LC_NUMERIC, "C");

  Manager manager;

  return a.exec();
}
