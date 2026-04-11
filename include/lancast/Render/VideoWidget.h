#pragma once

#include <QWidget>
#include <QImage>
#include <QTimer>
#include <memory>
#include "Common/FrameBuffer.h"

namespace lancast {

// Video rendering widget using Qt
class VideoWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget();

    // Display a video frame
    void displayFrame(const VideoFramePtr& frame);

    // Clear the display
    void clear();

    // Get recommended size
    QSize recommendedSize() const;

signals:
    void frameDisplayed();  // Emitted when a frame is displayed

protected:
    // Override paintEvent to draw the frame
    void paintEvent(QPaintEvent* event) override;

    // Override resizeEvent to handle window resizing
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateImage();

    QImage current_image_;
    QImage scaled_image_;
    QTimer repaint_timer_;

    VideoFramePtr current_frame_;
    int display_width_ = 0;
    int display_height_ = 0;
};

}  // namespace lancast
