/*  Copyright: Christian Feldmann (christian.feldmann@bitmovin.com)
 */

#include "ViewWidget.h"

#include <QPainter>
#include <QPalette>
#include <QTimerEvent>

#define DEBUG_WIDGET 0
#if DEBUG_WIDGET
#include <QDebug>
#define DEBUG(f) qDebug() << f
#else
#define DEBUG(f) ((void)0)
#endif

namespace
{

constexpr auto INFO_MESSAGE_TIMEOUT = std::chrono::seconds(10);

}

ViewWidget::ViewWidget(QWidget *parent) : QWidget(parent)
{
  auto pal = this->palette();
  pal.setColor(QPalette::Window, Qt::black);
  this->setAutoFillBackground(true);
  this->setPalette(pal);
}

void ViewWidget::setPlaybackController(PlaybackController *playbackController)
{
  assert(playbackController != nullptr);
  this->playbackController = playbackController;
}

void ViewWidget::addMessage(QString message, LoggingPriority priority)
{
  std::scoped_lock lock(this->messagesMutex);

  ViewWidgetMessage msg;
  msg.message   = message;
  msg.priority  = priority;
  msg.timeAdded = std::chrono::steady_clock::now();
  this->messages.push_back(msg);
}

void ViewWidget::paintEvent(QPaintEvent *)
{
  QPainter painter(this);

  if (this->curFrame.isNull())
    return;

  auto &rgbImage = this->curFrame.frame->rgbImage;
  if (!rgbImage.isNull())
  {
    int x = (this->width() - rgbImage.width()) / 2;
    int y = (this->height() - rgbImage.height()) / 2;
    painter.drawImage(x, y, rgbImage);
  }

  this->drawAndUpdateMessages(painter);
  this->drawFPSAndStatusText(painter);
  this->drawProgressGraph(painter);
}

void ViewWidget::drawAndUpdateMessages(QPainter &painter)
{
  unsigned           yOffset = 0;
  constexpr unsigned MARGIN  = 2;

  auto it = this->messages.begin();
  while (it != this->messages.end())
  {
    auto age = std::chrono::steady_clock::now() - it->timeAdded;
    if (it->priority == LoggingPriority::Info && age > INFO_MESSAGE_TIMEOUT)
    {
      it = this->messages.erase(it);
      continue;
    }

    auto text     = it->message;
    auto textSize = QFontMetrics(painter.font()).size(0, text);

    QRect textRect;
    textRect.setSize(textSize);
    textRect.moveTopLeft(QPoint(MARGIN, yOffset + MARGIN));

    // Draw the colored background box
    auto       boxRect = textRect + QMargins(MARGIN, MARGIN, MARGIN, MARGIN);
    const auto colorMap =
        std::map<LoggingPriority, QColor>({{LoggingPriority::Info, Qt::cyan},
                                           {LoggingPriority::Warning, Qt::darkYellow},
                                           {LoggingPriority::Error, Qt::darkRed}});
    painter.setBrush(colorMap.at(it->priority));
    painter.drawRect(boxRect);

    painter.drawText(textRect, Qt::AlignRight, text);

    yOffset += boxRect.height();
    it++;
  }
}

void ViewWidget::drawFPSAndStatusText(QPainter &painter)
{
  auto text = QString("FPS: %1\n").arg(this->actualFPS);
  if (this->playbackController && this->showDebugInfo)
    text += this->playbackController->getStatus();
  auto textSize = QFontMetrics(painter.font()).size(0, text);

  QRect textRect;
  textRect.setSize(textSize);
  textRect.moveTopRight(QPoint(this->width(), 0));

  auto boxRect = textRect + QMargins(5, 5, 5, 5);
  painter.setPen(Qt::white);
  painter.drawText(textRect, Qt::AlignLeft, text);
}

void ViewWidget::drawProgressGraph(QPainter &painter)
{
  if (!this->playbackController || !this->showProgressGraph)
    return;

  QRect graphRect;
  graphRect.setSize(QSize(500, 300));
  graphRect.moveBottomLeft(QPoint(0, this->height()));

  // painter.setBrush(Qt::white);
  // painter.drawRect(graphRect);

  // constexpr unsigned blockDistance = 3 + 1;
  // QRect frameRect;
  // frameRect.setSize(QSize(3, 3));
  // frameRect.moveBottom(graphRect.bottom() - 5);

  // const auto colorMap =
  //       std::map<FrameState, QColor>({{FrameState::DownloadQueued, Qt::cyan},
  //                                          {FrameState::Downloaded, Qt::blue},
  //                                          {FrameState::Decoded, Qt::yellow},
  //                                          {FrameState::Converted, Qt::green}});

  // const auto leftStart = graphRect.left() + 5;
  // auto info = this->playbackController->getFrameQueueInfo();
  // for (size_t i = 0; i < info.size(); i++)
  // {
  //   frameRect.moveLeft(leftStart + int(blockDistance * i));

  //   if (info[i].isBeingProcessed)
  //     painter.setPen(Qt::black);
  //   else
  //     painter.setPen(Qt::NoPen);

  //   painter.setBrush(colorMap.at(info[i].frameState));

  //   painter.drawRect(frameRect);
  // }

  // // Next the graph
  // auto segmentData = this->playbackController->getLastSegmentsData();

  // painter.setPen(Qt::black);
  // painter.setBrush(Qt::cyan);

  // QRect segmentRect;
  // segmentRect.setWidth(blockDistance * 24);

  // auto nrSegmentsToDraw = (info.size() + 23) / 24;
  // auto segmentIt = segmentData.rbegin();
  // for (size_t i = 0; i < nrSegmentsToDraw; i++)
  // {
  //   segmentRect.setHeight(int(segmentIt->bitrate) / 5000);
  //   segmentRect.moveBottom(graphRect.bottom() - 15);
  //   auto xOffset = ((nrSegmentsToDraw - i) * 24 - this->frameSegmentOffset) * blockDistance;
  //   segmentRect.moveLeft(int(xOffset));

  //   painter.drawRect(segmentRect);

  //   segmentIt++;
  //   if (segmentIt == segmentData.rend())
  //     break;
  // }
}

void ViewWidget::clearMessages()
{
  this->messages.clear();
  this->update();
}

void ViewWidget::setShowDebugInfo(bool showDebugInfo)
{
  this->showDebugInfo = showDebugInfo;
  this->update();
}

void ViewWidget::setShowProgressGraph(bool showGraph)
{
  this->showProgressGraph = showGraph;
  this->update();
}

void ViewWidget::setPlaybackFps(double framerate)
{
  this->targetFPS = framerate;

  if (framerate == 0.0)
    timer.stop();
  else
  {
    auto timerInterval = int(1000.0 / this->targetFPS);
    timer.start(timerInterval, Qt::PreciseTimer, this);
  }
}

void ViewWidget::timerEvent(QTimerEvent *event)
{
  if (event && event->timerId() != timer.timerId())
    return QWidget::timerEvent(event);
  if (this->playbackController == nullptr)
    return;

  auto                         segmentBuffer = this->playbackController->getSegmentBuffer();
  SegmentBuffer::FrameIterator displayFrame;
  if (this->curFrame.isNull())
    displayFrame = segmentBuffer->getFirstFrameToDisplay();
  else
    displayFrame = segmentBuffer->getNextFrameToDisplay(this->curFrame);
  if (displayFrame.isNull())
  {
    DEBUG("Timer even. No new image available.");
    return;
  }

  DEBUG("Timer even. Got next image");
  this->curFrame = displayFrame;

  // Update the FPS counter every 50 frames
  this->timerFPSCounter++;
  if (this->timerFPSCounter >= 50)
  {
    auto   newFrameTime         = QTime::currentTime();
    double msecsSinceLastUpdate = (double)this->timerLastFPSTime.msecsTo(newFrameTime);

    // Print the frames per second as float with one digit after the decimal dot.
    if (msecsSinceLastUpdate == 0)
      this->actualFPS = 0.0;
    else
      this->actualFPS = (50.0 / (msecsSinceLastUpdate / 1000.0));

    this->timerLastFPSTime = QTime::currentTime();
    this->timerFPSCounter  = 0;
  }

  this->frameSegmentOffset++;
  if (this->frameSegmentOffset > 24)
    this->frameSegmentOffset = 0;

  this->update();
}
