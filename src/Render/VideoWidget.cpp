#include "Render/VideoWidget.h"
#include "Common/FrameBuffer.h"
#include <QPainter>
#include <QResizeEvent>

namespace lancast {

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
{
    // Set widget attributes
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setMinimumSize(320, 240);

    // Timer for repainting at 30fps max
    repaint_timer_.setInterval(33);  // ~30fps
    connect(&repaint_timer_, &QTimer::timeout, this, &VideoWidget::updateImage);
    repaint_timer_.start();
}

VideoWidget::~VideoWidget() {
    repaint_timer_.stop();
}

void VideoWidget::displayFrame(const VideoFramePtr& frame) {
    current_frame_ = frame;
    if (frame) {
        display_width_ = frame->width_;
        display_height_ = frame->height_;

        // Convert YUV420P to QImage
        QImage::Format fmt = QImage::Format_RGB888;

        // Create a RGB888 QImage from YUV420P
        QImage rgb(display_width_, display_height_, fmt);

        const uint8_t* y = frame->yData();
        const uint8_t* u = frame->uData();
        const uint8_t* v = frame->vData();

        for (int h = 0; h < display_height_; ++h) {
            for (int w = 0; w < display_width_; ++w) {
                int y_val = y[h * display_width_ + w];
                int u_val = u[(h/2) * (display_width_/2) + (w/2)] - 128;
                int v_val = v[(h/2) * (display_width_/2) + (w/2)] - 128;

                // YUV to RGB conversion
                int r = std::clamp(y_val + 1.402 * v_val, 0.0, 255.0);
                int g = std::clamp(y_val - 0.344 * u_val - 0.714 * v_val, 0.0, 255.0);
                int b = std::clamp(y_val + 1.772 * u_val, 0.0, 255.0);

                rgb.setPixel(w, h, qRgb(r, g, b));
            }
        }

        current_image_ = rgb;
        scaled_image_ = rgb.scaled(size(), Qt::KeepAspectRatio, Qt::FastTransformation);
    }
}

void VideoWidget::clear() {
    current_frame_ = nullptr;
    current_image_ = QImage();
    scaled_image_ = QImage();
    update();
}

QSize VideoWidget::recommendedSize() const {
    return QSize(display_width_, display_height_);
}

void VideoWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);

    // Fill background with black
    painter.fillRect(rect(), Qt::black);

    if (!scaled_image_.isNull()) {
        // Center the image
        int x = (width() - scaled_image_.width()) / 2;
        int y = (height() - scaled_image_.height()) / 2;
        painter.drawImage(x, y, scaled_image_);
    }
}

void VideoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    // Rescale the image to new size
    if (!current_image_.isNull()) {
        scaled_image_ = current_image_.scaled(size(), Qt::KeepAspectRatio, Qt::FastTransformation);
    }
}

void VideoWidget::updateImage() {
    if (!current_image_.isNull()) {
        update();
    }
}

}  // namespace lancast
