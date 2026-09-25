#pragma once

#include "core/emulation/FrameMailbox.h"

#include <QQuickItem>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <memory>

class GameSurface : public QQuickItem {
    Q_OBJECT

public:
    explicit GameSurface(QQuickItem *parent = nullptr);

    static QRectF fitRect(const QSizeF &container, qreal aspectRatio = 4.0 / 3.0);

    void setFrameMailbox(std::shared_ptr<FrameMailbox> mailbox);
    void acceptFrame(std::shared_ptr<const VideoFrame> frame);
    std::uint64_t displayedSequence() const noexcept;

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    FrameMailbox acceptedFrames_;
    std::shared_ptr<FrameMailbox> frameMailbox_;
    QTimer refreshTimer_;
    std::atomic<std::uint64_t> acceptedSequence_{0};
    std::atomic<std::uint64_t> renderedSequence_{0};
};
