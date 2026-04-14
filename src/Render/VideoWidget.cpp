#include "Render/VideoWidget.h"
#include "Common/Logger.h"
#include "Common/FrameBuffer.h"
#include <QPainter>
#include <QResizeEvent>
#include <cstring>

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
    static uint64_t received_frames = 0;
    if (!frame) {
        return;
    }

    // Keep only the newest frame to avoid unbounded event backlog memory growth.
    std::lock_guard<std::mutex> lock(frame_mutex_);
    pending_frame_ = frame;
    if ((++received_frames % 30) == 1) Logger::log("VideoWidget::displayFrame pts=" + std::to_string(frame->pts_) + " size=" + std::to_string(frame->width_) + "x" + std::to_string(frame->height_));
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
        scaled_image_ = current_image_.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
}

void VideoWidget::updateImage() {
    static uint64_t painted_frames = 0;
    VideoFramePtr frame;
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (pending_frame_) {
            frame = std::move(pending_frame_);
            pending_frame_.reset();
        }
    }

    if (frame) {
        current_frame_ = frame;
        display_width_ = frame->width_;
        display_height_ = frame->height_;

        convertYuv420ToRgbImage(frame, current_image_);
        scaled_image_ = current_image_.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if ((++painted_frames % 30) == 1) Logger::log("VideoWidget::updateImage painted size=" + std::to_string(display_width_) + "x" + std::to_string(display_height_));
    }

    if (!scaled_image_.isNull()) {
        update();
    }
}

void VideoWidget::convertYuv420ToRgbImage(const VideoFramePtr& frame, QImage& out) {
    if (!frame || frame->width_ <= 0 || frame->height_ <= 0) {
        return;
    }

    out = QImage(frame->width_, frame->height_, QImage::Format_RGB888);
    if (out.isNull()) {
        return;
    }

    const int w = frame->width_;
    const int h = frame->height_;
    const uint8_t* y_plane = frame->yData();
    const uint8_t* u_plane = frame->uData();
    const uint8_t* v_plane = frame->vData();

    auto clamp_u8 = [](int v) -> uint8_t {
        if (v < 0) return 0;
        if (v > 255) return 255;
        return static_cast<uint8_t>(v);
    };

    for (int row = 0; row < h; ++row) {
        uint8_t* dst = out.scanLine(row);
        const int uv_row = row / 2;
        for (int col = 0; col < w; ++col) {
            const int y = static_cast<int>(y_plane[row * w + col]);
            const int uv_col = col / 2;
            const int u = static_cast<int>(u_plane[uv_row * (w / 2) + uv_col]) - 128;
            const int v = static_cast<int>(v_plane[uv_row * (w / 2) + uv_col]) - 128;

            // Integer BT.601 approximation.
            const int c = y - 16;
            const int d = u;
            const int e = v;

            const int r = (298 * c + 409 * e + 128) >> 8;
            const int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            const int b = (298 * c + 516 * d + 128) >> 8;

            dst[col * 3 + 0] = clamp_u8(r);
            dst[col * 3 + 1] = clamp_u8(g);
            dst[col * 3 + 2] = clamp_u8(b);
        }
    }
}

}  // namespace lancast



