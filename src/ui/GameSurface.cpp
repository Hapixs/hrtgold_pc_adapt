#include "ui/GameSurface.h"

#include <QImage>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

#include <algorithm>
#include <cmath>

GameSurface::GameSurface(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents);
    setSmooth(false);
    refreshTimer_.setInterval(16);
    connect(&refreshTimer_, &QTimer::timeout, this, &QQuickItem::update);
    connect(this, &QQuickItem::smoothChanged, this, &QQuickItem::update);
}

QRectF GameSurface::fitRect(const QSizeF &container, qreal aspectRatio) {
    const auto width = container.width();
    const auto height = container.height();
    if (!std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(aspectRatio) || width <= 0 || height <= 0 || aspectRatio <= 0) {
        return {};
    }

    const auto imageWidth = std::min(width, height * aspectRatio);
    const auto imageHeight = imageWidth / aspectRatio;
    return {(width - imageWidth) / 2, (height - imageHeight) / 2,
            imageWidth, imageHeight};
}

void GameSurface::setFrameMailbox(std::shared_ptr<FrameMailbox> mailbox) {
    if (frameMailbox_ != mailbox) {
        renderedSequence_.store(0, std::memory_order_release);
        acceptedSequence_.store(0, std::memory_order_release);
    }
    frameMailbox_ = std::move(mailbox);
    if (frameMailbox_) {
        refreshTimer_.start();
    } else {
        refreshTimer_.stop();
    }
    update();
}

void GameSurface::acceptFrame(std::shared_ptr<const VideoFrame> frame) {
    if (!frame || frame->width != kDsScreenWidth || frame->height != kDsScreenHeight ||
        frame->sequence <= displayedSequence()) {
        return;
    }
    acceptedFrames_.publish(std::move(frame));
    const auto snapshot = acceptedFrames_.latest();
    if (snapshot) {
        acceptedSequence_.store(snapshot->sequence, std::memory_order_release);
        update();
    }
}

std::uint64_t GameSurface::displayedSequence() const noexcept {
    return std::max(acceptedSequence_.load(std::memory_order_acquire),
                    renderedSequence_.load(std::memory_order_acquire));
}

QSGNode *GameSurface::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    const auto frame = frameMailbox_ ? frameMailbox_->latest() : acceptedFrames_.latest();

    if (frame && frame->width == kDsScreenWidth && frame->height == kDsScreenHeight &&
        frame->sequence > renderedSequence_.load(std::memory_order_acquire) && window()) {
        QImage image(kDsScreenWidth, kDsScreenHeight, QImage::Format_RGBA8888);
        for (int y = 0; y < kDsScreenHeight; ++y) {
            auto *row = image.scanLine(y);
            for (int x = 0; x < kDsScreenWidth; ++x) {
                const auto color = frame->rgba[static_cast<std::size_t>(y * kDsScreenWidth + x)];
                const auto offset = x * 4;
                row[offset] = static_cast<uchar>(color >> 16);
                row[offset + 1] = static_cast<uchar>(color >> 8);
                row[offset + 2] = static_cast<uchar>(color);
                row[offset + 3] = static_cast<uchar>(color >> 24);
            }
        }

        if (auto *texture = window()->createTextureFromImage(image)) {
            if (!node) {
                node = new QSGSimpleTextureNode;
                node->setTexture(texture);
            } else {
                auto *previous = node->texture();
                node->setOwnsTexture(false);
                node->setTexture(texture);
                delete previous;
            }
            node->setOwnsTexture(true);
            renderedSequence_.store(frame->sequence, std::memory_order_release);
        }
    }

    if (node) {
        node->setRect(fitRect(boundingRect().size()));
        node->setFiltering(smooth() ? QSGTexture::Linear : QSGTexture::Nearest);
    }
    return node;
}
