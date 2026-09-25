#include "ui/GameSurface.h"

#include <QtTest>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>

#include <cstdint>
#include <memory>

namespace {
std::shared_ptr<VideoFrame> solidFrame(std::uint64_t sequence, std::uint32_t color) {
    auto frame = std::make_shared<VideoFrame>();
    frame->sequence = sequence;
    frame->rgba.fill(color);
    return frame;
}
} // namespace

class InspectableGameSurface final : public GameSurface {
public:
    using GameSurface::updatePaintNode;
};

class GameSurfaceTest final : public QObject {
    Q_OBJECT

private slots:
    void fitRectPreservesFourByThree_data() {
        QTest::addColumn<QSizeF>("container");
        QTest::addColumn<QRectF>("expected");
        QTest::newRow("720p") << QSizeF(1280, 720) << QRectF(160, 0, 960, 720);
        QTest::newRow("1080p") << QSizeF(1920, 1080) << QRectF(240, 0, 1440, 1080);
        QTest::newRow("ultrawide") << QSizeF(3440, 1440) << QRectF(760, 0, 1920, 1440);
        QTest::newRow("4k") << QSizeF(3840, 2160) << QRectF(480, 0, 2880, 2160);
        QTest::newRow("portrait") << QSizeF(600, 900) << QRectF(0, 225, 600, 450);
        QTest::newRow("zero") << QSizeF(0, 900) << QRectF();
    }

    void fitRectPreservesFourByThree() {
        QFETCH(QSizeF, container);
        QFETCH(QRectF, expected);
        QCOMPARE(GameSurface::fitRect(container), expected);
    }

    void malformedAndStaleFramesKeepLastValidSequence() {
        GameSurface surface;
        surface.acceptFrame(solidFrame(5, 0xff112233));
        auto wrongWidth = solidFrame(6, 0xff445566);
        wrongWidth->width = 128;
        auto wrongHeight = solidFrame(7, 0xff778899);
        wrongHeight->height = 96;
        surface.acceptFrame(nullptr);
        surface.acceptFrame(wrongWidth);
        surface.acceptFrame(wrongHeight);
        surface.acceptFrame(solidFrame(5, 0xffabcdef));
        surface.acceptFrame(solidFrame(4, 0xffabcdef));
        QCOMPARE(surface.displayedSequence(), std::uint64_t{5});
    }

    void mailboxFrameRendersOnlyUpperPixelsWithLetterboxing() {
        auto mailbox = std::make_shared<FrameMailbox>();
        QQuickWindow window;
        window.setColor(Qt::black);
        window.resize(400, 400);
        auto *surface = new GameSurface(window.contentItem());
        surface->setWidth(400);
        surface->setHeight(400);
        surface->setFrameMailbox(mailbox);

        auto frame = solidFrame(1, 0xff2040c0);
        frame->rgba.front() = 0xffff0000;
        frame->rgba.back() = 0xff00ff00;
        mailbox->publish(frame);
        window.show();

        QTRY_COMPARE_WITH_TIMEOUT(surface->displayedSequence(), std::uint64_t{1}, 3000);
        QImage image;
        QTRY_VERIFY_WITH_TIMEOUT(!(image = window.grabWindow()).isNull(), 3000);
        QCOMPARE(image.pixelColor(200, 200), QColor(32, 64, 192));
        QCOMPARE(image.pixelColor(1, 1), QColor(Qt::black));
        QCOMPARE(image.pixelColor(1, 51), QColor(255, 0, 0));
        QCOMPARE(image.pixelColor(398, 348), QColor(0, 255, 0));
        QCOMPARE(image.pixelColor(398, 398), QColor(Qt::black));
    }

    void filteringOptionControlsSceneTexture() {
        InspectableGameSurface surface;
        QSGSimpleTextureNode node;
        QCOMPARE(surface.updatePaintNode(&node, nullptr), &node);
        QCOMPARE(node.filtering(), QSGTexture::Nearest);

        surface.setSmooth(true);
        QCOMPARE(surface.updatePaintNode(&node, nullptr), &node);
        QCOMPARE(node.filtering(), QSGTexture::Linear);
    }

    void aNewMailboxCanRestartFrameSequence() {
        auto first = std::make_shared<FrameMailbox>();
        auto second = std::make_shared<FrameMailbox>();
        QQuickWindow window;
        window.resize(256, 192);
        auto *surface = new GameSurface(window.contentItem());
        surface->setWidth(256);
        surface->setHeight(192);
        surface->setFrameMailbox(first);
        first->publish(solidFrame(10, 0xffff0000));
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(surface->displayedSequence(), std::uint64_t{10}, 3000);
        QCOMPARE(window.grabWindow().pixelColor(128, 96), QColor(Qt::red));

        second->publish(solidFrame(1, 0xff0000ff));
        surface->setFrameMailbox(second);
        QTRY_COMPARE_WITH_TIMEOUT(window.grabWindow().pixelColor(128, 96), QColor(Qt::blue), 3000);
    }

    void switchingToEmptyMailboxClearsOldPixels() {
        auto populated = std::make_shared<FrameMailbox>();
        auto empty = std::make_shared<FrameMailbox>();
        QQuickWindow window;
        window.setColor(Qt::black);
        window.resize(256, 192);
        auto *surface = new GameSurface(window.contentItem());
        surface->setSize(QSizeF(256, 192));
        surface->setFrameMailbox(populated);
        populated->publish(solidFrame(8, 0xffff0000));
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(window.grabWindow().pixelColor(128, 96), QColor(Qt::red), 3000);

        surface->setFrameMailbox(empty);
        QTRY_COMPARE_WITH_TIMEOUT(window.grabWindow().pixelColor(128, 96), QColor(Qt::black), 1000);
        QCOMPARE(surface->displayedSequence(), std::uint64_t{0});
    }

    void detachingMailboxClearsOldPixels() {
        auto mailbox = std::make_shared<FrameMailbox>();
        QQuickWindow window;
        window.setColor(Qt::black);
        window.resize(256, 192);
        auto *surface = new GameSurface(window.contentItem());
        surface->setSize(QSizeF(256, 192));
        surface->setFrameMailbox(mailbox);
        mailbox->publish(solidFrame(8, 0xffff0000));
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(window.grabWindow().pixelColor(128, 96), QColor(Qt::red), 3000);

        surface->setFrameMailbox(nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(window.grabWindow().pixelColor(128, 96), QColor(Qt::black), 1000);
        QCOMPARE(surface->displayedSequence(), std::uint64_t{0});
    }

    void directFramesAreIgnoredWhileMailboxIsActive() {
        auto mailbox = std::make_shared<FrameMailbox>();
        QQuickWindow window;
        window.setColor(Qt::black);
        window.resize(256, 192);
        auto *surface = new GameSurface(window.contentItem());
        surface->setSize(QSizeF(256, 192));
        surface->setFrameMailbox(mailbox);
        mailbox->publish(solidFrame(2, 0xff0000ff));
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(window.grabWindow().pixelColor(128, 96), QColor(Qt::blue), 3000);

        surface->acceptFrame(solidFrame(9, 0xff00ff00));
        QCOMPARE(surface->displayedSequence(), std::uint64_t{2});
        QCOMPARE(window.grabWindow().pixelColor(128, 96), QColor(Qt::blue));

        surface->setFrameMailbox(nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(window.grabWindow().pixelColor(128, 96), QColor(Qt::black), 1000);
        QCOMPARE(surface->displayedSequence(), std::uint64_t{0});
    }

    void detachStartsWithFreshDirectFrames() {
        QQuickWindow window;
        window.setColor(Qt::black);
        window.resize(256, 192);
        auto *surface = new GameSurface(window.contentItem());
        surface->setSize(QSizeF(256, 192));
        surface->acceptFrame(solidFrame(7, 0xffff0000));
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(window.grabWindow().pixelColor(128, 96), QColor(Qt::red), 3000);

        auto mailbox = std::make_shared<FrameMailbox>();
        mailbox->publish(solidFrame(2, 0xff0000ff));
        surface->setFrameMailbox(mailbox);
        QTRY_COMPARE_WITH_TIMEOUT(window.grabWindow().pixelColor(128, 96), QColor(Qt::blue), 3000);

        surface->setFrameMailbox(nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(window.grabWindow().pixelColor(128, 96), QColor(Qt::black), 1000);
        surface->acceptFrame(solidFrame(1, 0xff00ff00));
        QTRY_COMPARE_WITH_TIMEOUT(window.grabWindow().pixelColor(128, 96), QColor(Qt::green), 3000);
        QCOMPARE(surface->displayedSequence(), std::uint64_t{1});
    }
};

QTEST_MAIN(GameSurfaceTest)
#include "tst_game_surface.moc"
