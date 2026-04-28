#include "manager.h"
#include "capturer.h"
#include "corrector.h"
#include "debug.h"
#include "recognizer.h"
#include "representer.h"
#include "settingseditor.h"
#include "settingsvalidator.h"
#include "task.h"
#include "translator.h"
#include "trayicon.h"
#include "updates.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QThread>
#include <QTimer>

namespace
{
#ifdef DEVELOP
const auto updatesUrl = "http://localhost:8081/updates.json";
#else
const auto updatesUrl = "";
#endif
const auto resultHideWaitUs = 300'000;
const auto autoCaptureIntervalMs = 1200;

QString normalizeCapturedText(const TaskPtr &task)
{
  if (!task)
    return {};

  auto text = task->corrected.simplified();
  if (text.isEmpty())
    text = task->recognized.simplified();
  return text.toLower();
}
}  // namespace
using Loader = update::Loader;

Manager::Manager()
  : models_(std::make_unique<CommonModels>())
  , settings_(std::make_unique<Settings>())
  , updater_(std::make_unique<update::Updater>(QVector<QUrl>{{updatesUrl}}))
  , autoCaptureTimer_(std::make_unique<QTimer>())
{
  SOFT_ASSERT(settings_, return );

  // updater components
  (void)QT_TRANSLATE_NOOP("QObject", "app");
  (void)QT_TRANSLATE_NOOP("QObject", "recognizers");
  (void)QT_TRANSLATE_NOOP("QObject", "correction");
  (void)QT_TRANSLATE_NOOP("QObject", "translators");

  tray_ = std::make_unique<TrayIcon>(*this, *settings_);
  capturer_ = std::make_unique<Capturer>(*this, *settings_, *models_);
  recognizer_ = std::make_unique<Recognizer>(*this, *settings_);
  translator_ = std::make_unique<Translator>(*this, *settings_);
  corrector_ = std::make_unique<Corrector>(*this, *settings_);
  representer_ =
      std::make_unique<Representer>(*this, *tray_, *settings_, *models_);
  qRegisterMetaType<TaskPtr>();

  autoCaptureTimer_->setSingleShot(true);
  QObject::connect(autoCaptureTimer_.get(), &QTimer::timeout, [this] {
    if (!autoCaptureEnabled_ || activeTaskCount_ > 0)
      return;
    if (!capturer_ || !capturer_->canCaptureLocked())
      return;
    repeatCapture();
  });

  settings_->load();
  updateSettings();

  if (settings_->showMessageOnStart)
    tray_->showInformation(QObject::tr("Screen translator started"));

  warnIfOutdated();

  QObject::connect(updater_.get(), &update::Updater::error,  //
                   tray_.get(), &TrayIcon::showError);
  QObject::connect(updater_.get(), &update::Updater::updatesAvailable,  //
                   tray_.get(), [this] {
                     tray_->showInformation(QObject::tr("Updates available"));
                   });
}

Manager::~Manager()
{
  SOFT_ASSERT(settings_, return );
  SOFT_ASSERT(updater_, return );
  if (updater_->lastUpdateCheck().isValid() &&
      settings_->lastUpdateCheck != updater_->lastUpdateCheck()) {
    settings_->lastUpdateCheck = updater_->lastUpdateCheck();
    settings_->saveLastUpdateCheck();
    LTRACE() << "saved last update time";
  }
}

void Manager::warnIfOutdated()
{
  const auto now = QDateTime::currentDateTime();
  const auto binaryInfo = QFileInfo(QApplication::applicationFilePath());
  const auto date = binaryInfo.fileTime(QFile::FileTime::FileBirthTime);
  const auto deadlineDays = 90;
  if (date.daysTo(now) < deadlineDays)
    return;
  const auto updateDate = settings_->lastUpdateCheck;
  if (updateDate.isValid() && updateDate.daysTo(now) < deadlineDays)
    return;
  tray_->showInformation(
      QObject::tr("Current version might be outdated.\n"
                  "Check for updates to silence this warning"));
}

void Manager::updateSettings()
{
  stopAutoCapture();
  LTRACE() << "updateSettings";
  SOFT_ASSERT(settings_, return );

  tray_->resetFatalError();
  tray_->setTaskActionsEnabled(false);

  settings_->writeTrace = setupTrace(settings_->writeTrace);
  setupProxy(*settings_);
  setupUpdates(*settings_);

  models_->update(settings_->tessdataPath, settings_->translatorsPath);

  tray_->updateSettings();
  capturer_->updateSettings();
  recognizer_->updateSettings();
  corrector_->updateSettings();
  translator_->updateSettings();
  representer_->updateSettings();

  tray_->setCaptureLockedEnabled(capturer_->canCaptureLocked());

  SettingsValidator validator;
  validator.correct(*settings_, *models_);
  const auto errors = validator.check(*settings_, *models_);
  if (errors.isEmpty())
    return;

  fatalError(QObject::tr("Incorrect settings found. Go to Settings"));
}

void Manager::setupProxy(const Settings &settings)
{
  if (settings.proxyType == ProxyType::System) {
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    return;
  }

  QNetworkProxyFactory::setUseSystemConfiguration(false);

  if (settings.proxyType == ProxyType::Disabled) {
    QNetworkProxy::setApplicationProxy({});
    return;
  }

  QNetworkProxy proxy;
  using T = QNetworkProxy::ProxyType;
  proxy.setType(settings.proxyType == ProxyType::Socks5 ? T::Socks5Proxy
                                                        : T::HttpProxy);
  proxy.setHostName(settings.proxyHostName);
  proxy.setPort(settings.proxyPort);
  proxy.setUser(settings.proxyUser);
  proxy.setPassword(settings.proxyPassword);
  QNetworkProxy::setApplicationProxy(proxy);
}

void Manager::setupUpdates(const Settings &settings)
{
  updater_->setExpansions({
      {"$translators$", settings.translatorsPath},
      {"$tessdata$", settings.tessdataPath},
      {"$hunspell$", settings.hunspellPath},
      {"$appdir$", QApplication::applicationDirPath()},
  });

  updater_->setAutoUpdate(settings.autoUpdateIntervalDays,
                          settings.lastUpdateCheck);
}

bool Manager::setupTrace(bool isOn)
{
  const auto oldFile = debug::traceFileName();

  if (!isOn) {
    debug::setTraceFileName({});
    debug::isTrace = qEnvironmentVariableIsSet("TRACE");

    if (!oldFile.isEmpty())
      QDesktopServices::openUrl(QUrl::fromLocalFile(oldFile));

    return false;
  }

  if (!oldFile.isEmpty())
    return true;

  const auto traceFile =
      QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
      QLatin1String("/screen-translator-") +
      QDateTime::currentDateTime().toString("yyyy-MM-dd-hh-mm-ss");

  if (!debug::setTraceFileName(traceFile)) {
    QMessageBox::warning(
        nullptr, {}, QObject::tr("Failed to set log file: %1").arg(traceFile));
    return false;
  }

  debug::isTrace = true;
  QMessageBox::information(
      nullptr, {}, QObject::tr("Started logging to file: %1").arg(traceFile));
  return true;
}

void Manager::finishTask(const TaskPtr &task)
{
  SOFT_ASSERT(task, return );
  LTRACE() << "finishTask" << task;

  --activeTaskCount_;
  tray_->setActiveTaskCount(activeTaskCount_);

  if (!task->isValid()) {
    tray_->showError(task->error);
    tray_->setTaskActionsEnabled(false);
    return;
  }

  tray_->showSuccess();
}

void Manager::captured(const TaskPtr &task)
{
  tray_->setCaptureLockedEnabled(capturer_->canCaptureLocked());
  tray_->blockActions(false);

  SOFT_ASSERT(task, return );
  LTRACE() << "captured" << task;

  ++activeTaskCount_;
  tray_->setActiveTaskCount(activeTaskCount_);

  if (!task->isValid()) {
    finishTask(task);
    return;
  }

  recognizer_->recognize(task);
}

void Manager::captureCanceled()
{
  tray_->setCaptureLockedEnabled(capturer_->canCaptureLocked());
  tray_->blockActions(false);
}

void Manager::recognized(const TaskPtr &task)
{
  SOFT_ASSERT(task, return );
  LTRACE() << "recognized" << task;

  if (!task->isValid()) {
    finishTask(task);
    return;
  }

  corrector_->correct(task);
}

void Manager::corrected(const TaskPtr &task)
{
  SOFT_ASSERT(task, return );
  LTRACE() << "corrected" << task;

  if (!task->isValid()) {
    finishTask(task);
    stopAutoCapture();
    return;
  }

  const auto normalized = normalizeCapturedText(task);
  if (normalized.isEmpty()) {
    finishTask(task);
    scheduleAutoCapture();
    return;
  }

  if (normalized == lastProcessedText_) {
    finishTask(task);
    scheduleAutoCapture();
    return;
  }

  lastProcessedText_ = normalized;

  if (!task->targetLanguage.isEmpty())
    translator_->translate(task);
  else
    translated(task);
}

void Manager::translated(const TaskPtr &task)
{
  SOFT_ASSERT(task, return );
  LTRACE() << "translated" << task;

  finishTask(task);

  representer_->represent(task);
  tray_->setTaskActionsEnabled(!task->isNull());
  scheduleAutoCapture();
}

void Manager::applySettings(const Settings &settings)
{
  SOFT_ASSERT(settings_, return );
  const auto lastUpdate = settings_->lastUpdateCheck;  // not handled in editor

  *settings_ = settings;

  settings_->lastUpdateCheck = lastUpdate;

  settings_->save();
  updateSettings();
}

void Manager::fatalError(const QString &text)
{
  stopAutoCapture();
  tray_->blockActions(false);
  tray_->showFatalError(text);
}

void Manager::capture()
{
  SOFT_ASSERT(capturer_, return );

  stopAutoCapture();
  lastProcessedText_.clear();
  tray_->blockActions(true);

  if (representer_->isVisible()) {
    representer_->hide();
    QThread::usleep(resultHideWaitUs);
  }

  capturer_->capture();
  tray_->setRepeatCaptureEnabled(true);
}

void Manager::repeatCapture()
{
  SOFT_ASSERT(capturer_, return );
  tray_->blockActions(true);
  capturer_->repeatCapture();
}

void Manager::captureLocked()
{
  SOFT_ASSERT(capturer_, return );

  stopAutoCapture();

  if (representer_->isVisible()) {
    representer_->hide();
    QThread::usleep(resultHideWaitUs);
  }

  capturer_->captureLocked();
}

void Manager::settings()
{
  stopAutoCapture();
  SettingsEditor editor(*this, *updater_);

  SOFT_ASSERT(settings_, return );
  editor.setSettings(*settings_);

  tray_->blockActions(true);
  auto result = editor.exec();
  tray_->blockActions(false);

  if (result != QDialog::Accepted)
    return;

  tray_->resetFatalError();

  const auto edited = editor.settings();
  applySettings(edited);
}

void Manager::showLast()
{
  SOFT_ASSERT(representer_, return );
  representer_->showLast();
}

void Manager::showTranslator()
{
  SOFT_ASSERT(translator_, return );
  translator_->show();
}

void Manager::copyLastToClipboard()
{
  SOFT_ASSERT(representer_, return );
  representer_->clipboardLast();
}

void Manager::scheduleAutoCapture()
{
  if (!capturer_ || !capturer_->canCaptureLocked())
    return;

  autoCaptureEnabled_ = true;
  autoCaptureTimer_->start(autoCaptureIntervalMs);
}

void Manager::stopAutoCapture()
{
  autoCaptureEnabled_ = false;
  if (autoCaptureTimer_)
    autoCaptureTimer_->stop();
}

void Manager::quit()
{
  stopAutoCapture();
  QApplication::quit();
}
