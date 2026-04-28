#pragma once

#include "stfwd.h"

#include <QPixmap>
#include <optional>

class Capturer
{
public:
  Capturer(Manager &manager, const Settings &settings,
           const CommonModels &models);
  ~Capturer();

  void capture();
  bool canCaptureLocked();
  void captureLocked();
  void repeatCapture();
  void updateSettings();

  void selected(const CaptureArea &area);
  void canceled();

private:
  void updatePixmap();
  bool isWaylandSession() const;
  std::optional<QRect> selectWaylandArea(QString *error) const;
  QPixmap captureWaylandPixmapWithPortal(QString *error) const;
  bool prepareWaylandSelection(QString *error);
  bool preferPortalCapture() const;
  QRect portalRectForDesktopRect(const QRect &desktopRect,
                                 const QSize &portalSize) const;
  TaskPtr captureWaylandArea(const QRect &area) const;
  TaskPtr captureWaylandAreaWithGrim(const QRect &area) const;
  TaskPtr captureWaylandAreaWithPortal(const QRect &area) const;
  QString portalHelperPath() const;
  void captureWayland();

  Manager &manager_;
  const Settings &settings_;
  QPixmap pixmap_;
  QPoint pixmapOffset_;
  QSize selectorViewSize_;
  QRect virtualDesktopRect_;
  std::unique_ptr<CaptureAreaSelector> selector_;
  std::optional<QRect> lockedArea_;
};
