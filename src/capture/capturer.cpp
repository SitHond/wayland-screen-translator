#include "capturer.h"
#include "capturearea.h"
#include "captureareaselector.h"
#include "debug.h"
#include "manager.h"
#include "settings.h"
#include "task.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>

namespace
{
constexpr int kSlurpTimeoutMs = 30000;
constexpr int kGrimTimeoutMs = 5000;
constexpr int kPortalTimeoutMs = 15000;

TaskPtr makeErrorTask(const QString &message)
{
  auto task = std::make_shared<Task>();
  task->error = message;
  task->sourceLanguage = QStringLiteral("eng");
  return task;
}

TaskPtr makeTaskFromPixmap(const QPixmap &pixmap, const QRect &area,
                           const Settings &settings)
{
  if (pixmap.isNull())
    return {};

  auto task = std::make_shared<Task>();
  task->generation = 0;
  task->useHunspell = settings.useHunspell;
  task->captured = pixmap;
  task->capturePoint = area.topLeft();
  task->sourceLanguage = settings.sourceLanguage;
  if (task->sourceLanguage.isEmpty())
    task->error += QObject::tr("No source language set");

  if (settings.doTranslation && !settings.translators.isEmpty()) {
    task->targetLanguage = settings.targetLanguage;
    task->translators = settings.translators;
    if (task->targetLanguage.isEmpty()) {
      task->error += (task->error.isEmpty() ? "" : ", ") +
                     QObject::tr("No target language set");
    }
  }

  return task;
}
}  // namespace

Capturer::Capturer(Manager &manager, const Settings &settings,
                   const CommonModels &models)
  : manager_(manager)
  , settings_(settings)
  , selectorViewSize_()
  , selector_(std::make_unique<CaptureAreaSelector>(*this, settings_, models,
                                                    pixmap_, pixmapOffset_,
                                                    selectorViewSize_))
{
}

Capturer::~Capturer() = default;

void Capturer::capture()
{
  if (isWaylandSession()) {
    captureWayland();
    return;
  }
  updatePixmap();
  SOFT_ASSERT(selector_, return );
  selector_->activate();
}

bool Capturer::canCaptureLocked()
{
  if (isWaylandSession())
    return lockedArea_.has_value();
  SOFT_ASSERT(selector_, return false);
  return selector_->hasLocked();
}

void Capturer::captureLocked()
{
  if (isWaylandSession()) {
    if (!lockedArea_) {
      manager_.captureCanceled();
      return;
    }
    auto task = captureWaylandArea(*lockedArea_);
    if (task)
      manager_.captured(task);
    else
      manager_.captureCanceled();
    return;
  }

  updatePixmap();
  SOFT_ASSERT(selector_, return );
  selector_->captureLocked();
}

void Capturer::updatePixmap()
{
  const auto screens = QApplication::screens();
  std::vector<QRect> screenRects;
  screenRects.reserve(screens.size());
  QRect rect;

  for (const QScreen *screen : screens) {
    const auto geometry = screen->geometry();
    screenRects.push_back(geometry);
    rect |= geometry;
  }

  QPixmap combined(rect.size());
  QPainter p(&combined);
  p.translate(-rect.topLeft());

  for (const auto screen : screens) {
    const auto geometry = screen->geometry();
    const auto pixmap =
        screen->grabWindow(0, 0, 0, geometry.width(), geometry.height());
    p.drawPixmap(geometry, pixmap);
  }

  SOFT_ASSERT(selector_, return );
  pixmap_ = combined;
  pixmapOffset_ = rect.topLeft();
  selectorViewSize_ = rect.size();
  virtualDesktopRect_ = rect;

  for (auto &r : screenRects) r.translate(-rect.topLeft());
  selector_->setScreenRects(screenRects);
}

void Capturer::repeatCapture()
{
  if (isWaylandSession()) {
    if (lockedArea_) {
      auto task = captureWaylandArea(*lockedArea_);
      if (task)
        manager_.captured(task);
      else
        manager_.captureCanceled();
      return;
    }
    captureWayland();
    return;
  }
  SOFT_ASSERT(selector_, return );
  selector_->activate();
}

void Capturer::updateSettings()
{
  if (isWaylandSession())
    return;
  SOFT_ASSERT(selector_, return );
  selector_->updateSettings();
}

void Capturer::selected(const CaptureArea &area)
{
  SOFT_ASSERT(selector_, return manager_.captureCanceled())
  selector_->hide();

  if (isWaylandSession()) {
    const auto desktopRect = area.rect().translated(pixmapOffset_);
    lockedArea_ = desktopRect;

    SOFT_ASSERT(!pixmap_.isNull(), return manager_.captureCanceled())
    const auto portalRect = portalRectForDesktopRect(desktopRect, pixmap_.size());
    auto task = makeTaskFromPixmap(pixmap_.copy(portalRect), desktopRect, settings_);
    if (task)
      manager_.captured(task);
    else
      manager_.captureCanceled();
    return;
  }

  SOFT_ASSERT(!pixmap_.isNull(), return manager_.captureCanceled())
  auto task = area.task(pixmap_, pixmapOffset_);
  if (task)
    manager_.captured(task);
  else
    manager_.captureCanceled();
}

void Capturer::canceled()
{
  if (!isWaylandSession()) {
    SOFT_ASSERT(selector_, return );
    selector_->hide();
  }
  manager_.captureCanceled();
}

bool Capturer::isWaylandSession() const
{
  return !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
}

std::optional<QRect> Capturer::selectWaylandArea(QString *error) const
{
  if (error)
    error->clear();
  QProcess process;
  process.start(QStringLiteral("slurp"), QStringList{});
  if (!process.waitForStarted(3000)) {
    const auto msg = QObject::tr("slurp failed to start: %1").arg(process.errorString());
    if (error)
      *error = msg;
    LERROR() << msg;
    return std::nullopt;
  }
  if (!process.waitForFinished(kSlurpTimeoutMs)) {
    process.kill();
    process.waitForFinished(1000);
    const auto msg = QObject::tr("slurp timed out");
    if (error)
      *error = msg;
    LERROR() << msg;
    return std::nullopt;
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    const auto stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    const auto msg = stderrText.isEmpty()
                         ? QObject::tr("slurp failed with exit code %1").arg(process.exitCode())
                         : QObject::tr("slurp failed: %1").arg(stderrText);
    if (error)
      *error = msg;
    LWARNING() << msg;
    return std::nullopt;
  }

  const auto output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  const auto parts = output.split(' ');
  if (parts.size() != 2) {
    const auto msg = QObject::tr("slurp returned unexpected geometry: %1").arg(output);
    if (error)
      *error = msg;
    return std::nullopt;
  }
  const auto origin = parts[0].split(',');
  const auto size = parts[1].split('x');
  if (origin.size() != 2 || size.size() != 2) {
    const auto msg = QObject::tr("slurp returned malformed geometry: %1").arg(output);
    if (error)
      *error = msg;
    return std::nullopt;
  }

  bool okX = false;
  bool okY = false;
  bool okW = false;
  bool okH = false;
  const auto x = origin[0].toInt(&okX);
  const auto y = origin[1].toInt(&okY);
  const auto w = size[0].toInt(&okW);
  const auto h = size[1].toInt(&okH);
  if (!okX || !okY || !okW || !okH || w < 3 || h < 3) {
    const auto msg = QObject::tr("slurp returned invalid geometry: %1").arg(output);
    if (error)
      *error = msg;
    return std::nullopt;
  }
  return QRect(x, y, w, h);
}

bool Capturer::preferPortalCapture() const
{
  if (!isWaylandSession())
    return false;

  // Portal capture is the most reliable path on modern Wayland sessions,
  // especially on Plasma where grim/slurp are often unavailable or broken.
  return true;
}

QRect Capturer::portalRectForDesktopRect(const QRect &desktopRect,
                                         const QSize &portalSize) const
{
  if (!virtualDesktopRect_.isValid() || !portalSize.isValid())
    return desktopRect;

  const auto local = desktopRect.translated(-virtualDesktopRect_.topLeft());
  const qreal scaleX = qreal(portalSize.width()) / qreal(virtualDesktopRect_.width());
  const qreal scaleY = qreal(portalSize.height()) / qreal(virtualDesktopRect_.height());

  QRect mapped(qRound(local.x() * scaleX), qRound(local.y() * scaleY),
               qRound(local.width() * scaleX), qRound(local.height() * scaleY));
  return mapped.intersected(QRect(QPoint(0, 0), portalSize));
}

TaskPtr Capturer::captureWaylandArea(const QRect &area) const
{
  if (preferPortalCapture()) {
    if (auto portalTask = captureWaylandAreaWithPortal(area); portalTask && portalTask->isValid())
      return portalTask;
    return makeErrorTask(QObject::tr("Wayland portal capture failed."));
  }

  if (auto grimTask = captureWaylandAreaWithGrim(area); grimTask && grimTask->isValid())
    return grimTask;

  if (auto portalTask = captureWaylandAreaWithPortal(area); portalTask && portalTask->isValid())
    return portalTask;

  return makeErrorTask(QObject::tr(
      "Wayland capture failed. Ensure slurp is installed and either grim works on your compositor or portal capture is available."));
}

TaskPtr Capturer::captureWaylandAreaWithGrim(const QRect &area) const
{
  const auto geometry = QStringLiteral("%1,%2 %3x%4")
                            .arg(area.x())
                            .arg(area.y())
                            .arg(area.width())
                            .arg(area.height());

  QProcess process;
  process.start(QStringLiteral("grim"),
                {QStringLiteral("-g"), geometry, QStringLiteral("-")});
  if (!process.waitForFinished(kGrimTimeoutMs)) {
    process.kill();
    process.waitForFinished(1000);
    LWARNING() << "grim timed out";
    return makeErrorTask(QObject::tr("grim timed out"));
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    const auto stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    LWARNING() << "grim failed" << stderrText;
    return makeErrorTask(QObject::tr("grim failed: %1").arg(stderrText));
  }

  QPixmap pixmap;
  if (!pixmap.loadFromData(process.readAllStandardOutput(), "PNG")) {
    LWARNING() << "grim returned invalid PNG";
    return makeErrorTask(QObject::tr("grim returned invalid PNG data"));
  }

  return makeTaskFromPixmap(pixmap, area, settings_);
}

QPixmap Capturer::captureWaylandPixmapWithPortal(QString *error) const
{
  if (error)
    error->clear();

  const auto helper = portalHelperPath();
  if (helper.isEmpty()) {
    const auto msg = QObject::tr("portal helper script not found");
    if (error)
      *error = msg;
    LWARNING() << msg;
    return {};
  }

  QProcess process;
  process.start(QStringLiteral("python3"), {helper});
  if (!process.waitForStarted(3000)) {
    const auto msg = QObject::tr("portal helper failed to start: %1").arg(process.errorString());
    if (error)
      *error = msg;
    LWARNING() << msg;
    return {};
  }
  if (!process.waitForFinished(kPortalTimeoutMs)) {
    process.kill();
    process.waitForFinished(1000);
    const auto msg = QObject::tr("portal helper timed out");
    if (error)
      *error = msg;
    LWARNING() << msg;
    return {};
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    const auto stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    const auto msg = stderrText.isEmpty()
                         ? QObject::tr("portal capture failed with exit code %1").arg(process.exitCode())
                         : QObject::tr("portal capture failed: %1").arg(stderrText);
    if (error)
      *error = msg;
    LWARNING() << msg;
    return {};
  }

  const auto screenshotPath = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  if (screenshotPath.isEmpty()) {
    const auto msg = QObject::tr("portal capture returned empty path");
    if (error)
      *error = msg;
    return {};
  }

  QPixmap fullPixmap(screenshotPath);
  if (fullPixmap.isNull()) {
    const auto msg = QObject::tr("failed to load portal screenshot: %1").arg(screenshotPath);
    if (error)
      *error = msg;
    return {};
  }

  return fullPixmap;
}

bool Capturer::prepareWaylandSelection(QString *error)
{
  pixmap_ = captureWaylandPixmapWithPortal(error);
  if (pixmap_.isNull())
    return false;

  const auto screens = QApplication::screens();
  std::vector<QRect> screenRects;
  screenRects.reserve(screens.size());
  QRect virtualRect;

  for (const QScreen *screen : screens) {
    const auto geometry = screen->geometry();
    screenRects.push_back(geometry);
    virtualRect |= geometry;
  }

  if (!virtualRect.isValid())
    virtualRect = QRect(QPoint(0, 0), pixmap_.size());

  // On Wayland/Plasma, portal screenshots often come back in physical pixels
  // while Qt screen geometries are logical coordinates. A mismatch here is
  // expected on mixed-DPI setups, so only treat clearly broken geometry as
  // suspicious.
  if (pixmap_.size() != virtualRect.size()) {
    const auto widthRatio =
        virtualRect.width() > 0
            ? qreal(pixmap_.width()) / qreal(virtualRect.width())
            : 1.0;
    const auto heightRatio =
        virtualRect.height() > 0
            ? qreal(pixmap_.height()) / qreal(virtualRect.height())
            : 1.0;

    const auto suspicious =
        widthRatio < 0.5 || widthRatio > 2.5 || heightRatio < 0.5 ||
        heightRatio > 2.5;
    if (suspicious) {
      LWARNING() << "portal screenshot size looks suspicious relative to "
                    "virtual desktop"
                 << pixmap_.size() << virtualRect.size() << widthRatio
                 << heightRatio;
    }
  }

  virtualDesktopRect_ = virtualRect;
  pixmapOffset_ = virtualRect.topLeft();
  selectorViewSize_ = virtualRect.size();

  for (auto &rect : screenRects)
    rect.translate(-pixmapOffset_);

  SOFT_ASSERT(selector_, return false);
  selector_->setScreenRects(screenRects.empty()
                                ? std::vector<QRect>{QRect(QPoint(0, 0), selectorViewSize_)}
                                : screenRects);
  selector_->updateSettings();
  return true;
}

TaskPtr Capturer::captureWaylandAreaWithPortal(const QRect &area) const
{
  QString error;
  QPixmap fullPixmap = captureWaylandPixmapWithPortal(&error);
  if (fullPixmap.isNull())
    return makeErrorTask(error.isEmpty() ? QObject::tr("portal capture failed") : error);

  const auto localArea = portalRectForDesktopRect(area, fullPixmap.size());
  if (localArea.width() < 3 || localArea.height() < 3) {
    return makeErrorTask(QObject::tr("portal screenshot area is outside the image bounds"));
  }

  return makeTaskFromPixmap(fullPixmap.copy(localArea), area, settings_);
}

QString Capturer::portalHelperPath() const
{
  const auto appDir = QCoreApplication::applicationDirPath();
  const QStringList candidates = {
      appDir + QLatin1String("/../share/screen-translator/wayland_portal_capture.py"),
      appDir + QLatin1String("/../share/wayland_portal_capture.py"),
      appDir + QLatin1String("/share/wayland_portal_capture.py"),
      QDir::currentPath() + QLatin1String("/share/wayland_portal_capture.py"),
      QDir::currentPath() + QLatin1String("/share/wayland_portal_capture.py"),
      QStandardPaths::locate(QStandardPaths::AppDataLocation,
                             QStringLiteral("wayland_portal_capture.py"))};

  for (const auto &candidate : candidates) {
    if (!candidate.isEmpty() && QFileInfo::exists(candidate))
      return candidate;
  }
  return {};
}

void Capturer::captureWayland()
{
  QString selectionError;
  if (!prepareWaylandSelection(&selectionError)) {
    const auto msg = selectionError.isEmpty()
                         ? QObject::tr("Wayland region selection failed.")
                         : selectionError;
    manager_.captured(makeErrorTask(msg));
    return;
  }

  SOFT_ASSERT(selector_, return manager_.captureCanceled())
  selector_->activate();
}
