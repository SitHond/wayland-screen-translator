#include "resultwidget.h"
#include "debug.h"
#include "manager.h"
#include "representer.h"
#include "settings.h"
#include "task.h"

#include <QApplication>
#include <QBoxLayout>
#include <QDesktopWidget>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSizePolicy>
#include <QWindow>

ResultWidget::ResultWidget(Manager &manager, Representer &representer,
                           const Settings &settings, QWidget *parent)
  : QFrame(parent)
  , representer_(representer)
  , settings_(settings)
  , imagePlaceholder_(new QWidget(this))
  , image_(new QLabel(imagePlaceholder_))
  , recognized_(new QLabel(this))
  , separator_(new QLabel(this))
  , translated_(new QLabel(this))
  , contextMenu_(new QMenu(this))
{
  Qt::WindowFlags flags = Qt::WindowStaysOnTopHint;
  if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
    flags |= Qt::Window;
  } else {
    flags |= Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;
    flags |= Qt::X11BypassWindowManagerHint;
  }
  setWindowFlags(flags);

  setAttribute(Qt::WA_TranslucentBackground, true);
  setAutoFillBackground(true);
  setAttribute(Qt::WA_NoSystemBackground, true);

  setLineWidth(0);
  setFrameShape(QFrame::NoFrame);
  setFrameShadow(QFrame::Plain);

  imagePlaceholder_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  image_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  recognized_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  translated_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  separator_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

  image_->setAlignment(Qt::AlignCenter);
  auto imageLayout = new QVBoxLayout(imagePlaceholder_);
  imageLayout->setContentsMargins(0, 0, 0, 0);
  imageLayout->addWidget(image_);

  recognized_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  recognized_->setWordWrap(true);
  recognized_->setMargin(0);

  separator_->setFixedHeight(1);
  separator_->setAutoFillBackground(true);

  translated_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  translated_->setWordWrap(true);
  translated_->setMargin(0);
  translated_->setTextInteractionFlags(Qt::NoTextInteraction);
  translated_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

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

  auto layout = new QVBoxLayout(this);
  layout->addWidget(imagePlaceholder_);
  layout->addWidget(recognized_);
  layout->addWidget(separator_);
  layout->addWidget(translated_);

  layout->setContentsMargins(18, 14, 18, 14);
  layout->setSpacing(0);

  updateSettings();
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

  auto palette = this->palette();
  const auto &backgroundColor = settings_.backgroundColor;
  palette.setColor(QPalette::Window, backgroundColor);
  palette.setColor(QPalette::Base, backgroundColor);
  palette.setColor(QPalette::WindowText, settings_.fontColor);
  palette.setColor(QPalette::Text, settings_.fontColor);
  setPalette(palette);

  const auto separatorColor = backgroundColor.lightness() > 150
                                  ? backgroundColor.darker()
                                  : backgroundColor.lighter();
  palette.setColor(QPalette::Window, separatorColor);
  separator_->setPalette(palette);

  imagePlaceholder_->setVisible(settings_.showCaptured);
  imagePlaceholder_->setMinimumSize(0, 0);
  imagePlaceholder_->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
  recognized_->setVisible(settings_.showRecognized);
  separator_->setVisible(settings_.showCaptured || settings_.showRecognized);

  recognized_->setStyleSheet(
      QStringLiteral("QLabel { color: rgba(%1, %2, %3, %4); "
                     "background: transparent; font-size: %5pt; line-height: 1.2; }")
          .arg(settings_.fontColor.red())
          .arg(settings_.fontColor.green())
          .arg(settings_.fontColor.blue())
          .arg(std::max(110, settings_.fontColor.alpha() - 70))
          .arg(std::max(10, settings_.fontSize - 3)));
  translated_->setStyleSheet(
      QStringLiteral("QLabel { color: rgba(%1, %2, %3, %4); "
                     "background: transparent; font-size: %5pt; line-height: 1.25; }")
          .arg(settings_.fontColor.red())
          .arg(settings_.fontColor.green())
          .arg(settings_.fontColor.blue())
          .arg(settings_.fontColor.alpha())
          .arg(settings_.fontSize));
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
  painter.setBrush(settings_.backgroundColor);
  painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 12, 12);
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
