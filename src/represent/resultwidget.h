#pragma once

#include "stfwd.h"

#include <QFrame>

class QLabel;
class QMenu;
class QPushButton;

class ResultWidget : public QFrame
{
  Q_OBJECT
public:
  ResultWidget(Manager& manager, Representer& representer,
               const Settings& settings, QWidget* parent = nullptr);

  const TaskPtr& task() const;
  void show(const TaskPtr& task);
  using QWidget::show;
  void updateSettings();

protected:
  void changeEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

private:
  void updateWindowButtons();
  void edit();
  void copyImage();
  void copyText();

  Representer& representer_;
  const Settings& settings_;
  TaskPtr task_;
  QWidget* contentPanel_;
  QWidget* titleBar_;
  QLabel* titleLabel_;
  QPushButton* minimizeButton_;
  QPushButton* maximizeButton_;
  QPushButton* closeButton_;
  QWidget* imagePlaceholder_;
  QLabel* image_;
  QLabel* recognized_;
  QLabel* separator_;
  QLabel* translated_;
  QMenu* contextMenu_;
  QPoint lastPos_;
  bool dragging_{false};
  bool geometryInitialized_{false};
};
