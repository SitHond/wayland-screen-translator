#include "resultwidget.h"
#include "debug.h"
#include "manager.h"
#include "representer.h"
#include "settings.h"
#include "task.h"

#include <QApplication>
#include <QBoxLayout>
#include <QDesktopWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QSizePolicy>
#include <QStyle>
#include <QWindow>

ResultWidget::ResultWidget(Manager &manager, Representer &representer,
                           const Settings &settings, QWidget *parent)
  : QFrame(parent)
  , representer_(representer)
  , settings_(settings)
  , contentPanel_(new QWidget(this))
  , titleBar_(new QWidget(contentPanel_))
  , titleLabel_(new QLabel(this))
  , minimizeButton_(new QPushButton(this))
  , maximizeButton_(new QPushButton(this))
  , closeButton_(new QPushButton(this))
  , imagePlaceholder_(new QWidget(this))
  , image_(new QLabel(imagePlaceholder_))
  , recognized_(new QLabel(this))
  , separator_(new QLabel(this))
  , translated_(new QLabel(this))
  , contextMenu_(new QMenu(this))
{
  Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint |
                          Qt::WindowStaysOnTopHint;
  if (!qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
    flags |= Qt::NoDropShadowWindowHint;
    flags |= Qt::X11BypassWindowManagerHint;
  }
  setWindowFlags(flags);

  setAttribute(Qt::WA_TranslucentBackground, true);
  setAutoFillBackground(true);
  setAttribute(Qt::WA_NoSystemBackground, true);

  setLineWidth(0);
  setFrameShape(QFrame::NoFrame);
  setFrameShadow(QFrame::Plain);

  contentPanel_->setObjectName(QStringLiteral("contentPanel"));
  contentPanel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

  titleBar_->setObjectName(QStringLiteral("titleBar"));
  titleBar_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

  titleLabel_->setObjectName(QStringLiteral("titleLabel"));
  titleLabel_->setText(tr("Translation"));
  titleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

  const auto setupButton = [](QPushButton *button, const QString &text,
                              const QString &name) {
    button->setObjectName(name);
    button->setText(text);
    button->setFlat(true);
    button->setCursor(Qt::ArrowCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(28, 24);
  };

  setupButton(minimizeButton_, QStringLiteral("—"),
              QStringLiteral("windowButton"));
  setupButton(maximizeButton_, QStringLiteral("□"),
              QStringLiteral("windowButton"));
  setupButton(closeButton_, QStringLiteral("×"),
              QStringLiteral("windowButtonClose"));

  connect(minimizeButton_, &QPushButton::clicked, this,
          [this] { showMinimized(); });
  connect(maximizeButton_, &QPushButton::clicked, this, [this] {
    isMaximized() ? showNormal() : showMaximized();
    updateWindowButtons();
  });
  connect(closeButton_, &QPushButton::clicked, this, [this] { hide(); });

  imagePlaceholder_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  image_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  recognized_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  translated_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  separator_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

  image_->setAlignment(Qt::AlignCenter);
  auto imageLayout = new QVBoxLayout(imagePlaceholder_);
  imageLayout->setContentsMargins(0, 0, 0, 0);
  imageLayout->addWidget(image_);

  auto titleLayout = new QHBoxLayout(titleBar_);
  titleLayout->setContentsMargins(2, 0, 0, 2);
  titleLayout->setSpacing(6);
  titleLayout->addWidget(titleLabel_);
  titleLayout->addStretch(1);
  titleLayout->addWidget(minimizeButton_);
  titleLayout->addWidget(maximizeButton_);
  titleLayout->addWidget(closeButton_);

  recognized_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  recognized_->setWordWrap(true);
  recognized_->setMargin(0);
  recognized_->setObjectName(QStringLiteral("recognizedText"));

  separator_->setFixedHeight(1);
  separator_->setAutoFillBackground(true);

  translated_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  translated_->setWordWrap(true);
  translated_->setMargin(0);
  translated_->setTextInteractionFlags(Qt::NoTextInteraction);
  translated_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  translated_->setObjectName(QStringLiteral("translatedText"));

  {
    auto clipboardText = contextMenu_->addAction(tr("Copy text"));
    connect(clipboardText, &QAction::triggered,  //
            this, &ResultWidget::copyText);
    auto clipboardImage = contextMenu_->addAction(tr("Copy image"));
    connect(clipboardImage, &QAction::triggered,  //
            this, &ResultWidget::copyImage);
    auto edit = contextMenu_->addAction(tr("Edit..."));
    connect(edit, &QAction::triggered,  //
            this, &ResultWidget::edit);

    contextMenu_->addSeparator();

    auto capture = contextMenu_->addAction(tr("New capture"));
    connect(capture, &QAction::triggered,  //
            this, [&manager] { manager.capture(); });
    auto repeatCapture = contextMenu_->addAction(tr("Repeat capture"));
    connect(repeatCapture, &QAction::triggered,  //
            this, [&manager] { manager.repeatCapture(); });
  }

  installEventFilter(this);

  auto contentLayout = new QVBoxLayout(contentPanel_);
  contentLayout->addWidget(titleBar_);
  contentLayout->addWidget(imagePlaceholder_);
  contentLayout->addWidget(recognized_);
  contentLayout->addWidget(separator_);
  contentLayout->addWidget(translated_);
  contentLayout->setContentsMargins(18, 14, 18, 14);
  contentLayout->setSpacing(9);

  auto layout = new QVBoxLayout(this);
  layout->addWidget(contentPanel_);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(0);

  updateSettings();
  updateWindowButtons();
}

const TaskPtr &ResultWidget::task() const
{
  return task_;
}

void ResultWidget::show(const TaskPtr &task)
{
  task_ = task;

  const auto text = task->translated.trimmed();
  if (text.isEmpty())
    return;

  const auto screen = QApplication::screenAt(task->capturePoint);
  SOFT_ASSERT(screen, return );
  const auto screenRect = screen->availableGeometry();
  const int maxWidth = std::clamp(screenRect.width() / 2, 320, 760);
  const int contentWidth = maxWidth - 36;

  const auto recognizedText = task->corrected.trimmed().isEmpty()
                                  ? task->recognized.trimmed()
                                  : task->corrected.trimmed();

  const bool showCapture = settings_.showCaptured && !task->captured.isNull();
  const bool showRecognized =
      settings_.showRecognized && !recognizedText.isEmpty();

  if (showCapture) {
    const auto maxImageSize = QSize(contentWidth, screenRect.height() / 3);
    image_->setPixmap(task->captured.scaled(maxImageSize, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation));
    imagePlaceholder_->show();
    image_->show();
  } else {
    image_->clear();
    imagePlaceholder_->hide();
  }

  if (showRecognized) {
    recognized_->setText(recognizedText);
    recognized_->setMaximumWidth(contentWidth);
    recognized_->show();
  } else {
    recognized_->clear();
    recognized_->hide();
  }

  translated_->setText(text);
  translated_->setToolTip(task->usedTranslator);
  translated_->setMaximumWidth(contentWidth);
  translated_->show();

  const auto showSeparator = showRecognized || showCapture;
  separator_->setVisible(showSeparator);

  translated_->setMinimumWidth(280);
  translated_->adjustSize();

  QWidget::show();
  layout()->activate();

  const auto targetSize = sizeHint().boundedTo(
      QSize(maxWidth, std::max(screenRect.height() - 24, 240)));

  if (!geometryInitialized_) {
    resize(targetSize);

    QRect rect(task->capturePoint, size());
    rect.translate(0, -height() - 16);

    if (rect.left() < screenRect.left())
      rect.moveLeft(screenRect.left() + 12);
    if (rect.right() > screenRect.right())
      rect.moveRight(screenRect.right() - 12);
    if (rect.top() < screenRect.top())
      rect.moveTop(
          std::min(screenRect.bottom() - height(), task->capturePoint.y() + 20));
    if (rect.bottom() > screenRect.bottom())
      rect.moveBottom(screenRect.bottom() - 12);

    move(rect.topLeft());
    geometryInitialized_ = true;
  } else {
    resize(std::max(width(), targetSize.width()), targetSize.height());
  }

  if (!qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
    activateWindow();
}

void ResultWidget::updateSettings()
{
  QFont font(settings_.fontFamily, settings_.fontSize);
  setFont(font);

  const QColor panelColor(102, 102, 102, 168);
  const QColor panelBorder(255, 255, 255, 24);

  auto palette = this->palette();
  palette.setColor(QPalette::Window, Qt::transparent);
  palette.setColor(QPalette::Base, Qt::transparent);
  palette.setColor(QPalette::WindowText, settings_.fontColor);
  palette.setColor(QPalette::Text, settings_.fontColor);
  setPalette(palette);

  const auto separatorColor = QColor(255, 255, 255, 36);
  palette.setColor(QPalette::Window, separatorColor);
  separator_->setPalette(palette);

  imagePlaceholder_->setVisible(settings_.showCaptured);
  imagePlaceholder_->setMinimumSize(0, 0);
  imagePlaceholder_->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
  recognized_->setVisible(settings_.showRecognized);
  separator_->setVisible(settings_.showCaptured || settings_.showRecognized);

  contentPanel_->setStyleSheet(
      QStringLiteral(
          "QWidget#contentPanel {"
          "background-color: rgba(%1, %2, %3, %4);"
          "border: 1px solid rgba(%5, %6, %7, %8);"
          "border-radius: 14px;"
          "}"
          "QWidget#titleBar {"
          "background: transparent;"
          "border: none;"
          "}"
          "QLabel#titleLabel {"
          "color: rgba(255, 255, 255, 150);"
          "background: transparent;"
          "font-size: %9pt;"
          "font-weight: 500;"
          "}"
          "QPushButton#windowButton, QPushButton#windowButtonClose {"
          "color: rgba(255, 255, 255, 188);"
          "background: rgba(255, 255, 255, 18);"
          "border: none;"
          "border-radius: 8px;"
          "padding: 0;"
          "font-size: %10pt;"
          "}"
          "QPushButton#windowButton:hover {"
          "background: rgba(255, 255, 255, 34);"
          "}"
          "QPushButton#windowButtonClose:hover {"
          "background: rgba(255, 110, 110, 90);"
          "}")
          .arg(panelColor.red())
          .arg(panelColor.green())
          .arg(panelColor.blue())
          .arg(panelColor.alpha())
          .arg(panelBorder.red())
          .arg(panelBorder.green())
          .arg(panelBorder.blue())
          .arg(panelBorder.alpha())
          .arg(std::max(9, settings_.fontSize - 7))
          .arg(std::max(10, settings_.fontSize - 6)));

  recognized_->setStyleSheet(
      QStringLiteral("QLabel { color: rgba(%1, %2, %3, %4); "
                     "background: transparent; font-size: %5pt; line-height: 1.2; }")
          .arg(settings_.fontColor.red())
          .arg(settings_.fontColor.green())
          .arg(settings_.fontColor.blue())
          .arg(std::max(105, settings_.fontColor.alpha() - 95))
          .arg(std::max(10, settings_.fontSize - 4)));
  translated_->setStyleSheet(
      QStringLiteral("QLabel { color: rgba(%1, %2, %3, %4); "
                     "background: transparent; font-size: %5pt; font-weight: 500; line-height: 1.22; }")
          .arg(settings_.fontColor.red())
          .arg(settings_.fontColor.green())
          .arg(settings_.fontColor.blue())
          .arg(settings_.fontColor.alpha())
          .arg(settings_.fontSize));
}

void ResultWidget::changeEvent(QEvent *event)
{
  if (event && event->type() == QEvent::WindowStateChange)
    updateWindowButtons();
  QFrame::changeEvent(event);
}

void ResultWidget::mousePressEvent(QMouseEvent *event)
{
  const auto button = event->button();
  if (button == Qt::RightButton) {
    contextMenu_->exec(QCursor::pos());
    return;
  }

  if (button == Qt::LeftButton || button == Qt::MiddleButton) {
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY") && windowHandle()) {
      windowHandle()->startSystemMove();
      event->accept();
      return;
    }

    dragging_ = true;
    lastPos_ = event->globalPos() - frameGeometry().topLeft();
    event->accept();
    return;
  }

  QFrame::mousePressEvent(event);
}

void ResultWidget::updateWindowButtons()
{
  maximizeButton_->setText(isMaximized() ? QStringLiteral("❐")
                                         : QStringLiteral("□"));
}

void ResultWidget::mouseMoveEvent(QMouseEvent *event)
{
  if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
    QFrame::mouseMoveEvent(event);
    return;
  }

  if (!dragging_) {
    QFrame::mouseMoveEvent(event);
    return;
  }

  move(event->globalPos() - lastPos_);
  event->accept();
}

void ResultWidget::mouseReleaseEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
    dragging_ = false;
    event->accept();
    return;
  }

  QFrame::mouseReleaseEvent(event);
}

void ResultWidget::paintEvent(QPaintEvent *event)
{
  Q_UNUSED(event);

  if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
    return;

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::transparent);
  painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 16, 16);
}

void ResultWidget::edit()
{
  representer_.edit(task_);
}

void ResultWidget::copyText()
{
  representer_.clipboardText(task_);
}

void ResultWidget::copyImage()
{
  representer_.clipboardImage(task_);
}
