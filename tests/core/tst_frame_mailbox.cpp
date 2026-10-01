#include "core/emulation/FrameMailbox.h"

#include <QtTest>

#include <cstdint>
#include <memory>

namespace {
std::shared_ptr<VideoFrame> makeFrame(std::uint64_t sequence, std::uint32_t color) {
    auto frame = std::make_shared<VideoFrame>();
    frame->sequence = sequence;
    frame->rgba.fill(color);
    return frame;
}
} // namespace

class FrameMailboxTest final : public QObject {
    Q_OBJECT

private slots:
    void mailboxReturnsNewestCompleteFrame() {
        FrameMailbox box;
        box.publish(makeFrame(7, 0xff112233));
        box.publish(makeFrame(8, 0xff445566));

        const auto frame = box.latest();

        QVERIFY(frame);
        QCOMPARE(frame->sequence, std::uint64_t{8});
        QCOMPARE(frame->rgba.front(), std::uint32_t{0xff445566});
        QCOMPARE(frame->rgba.back(), std::uint32_t{0xff445566});
    }

    void rejectsNullAndMalformedFrames() {
        FrameMailbox box;
        const auto valid = makeFrame(4, 0xff010203);
        box.publish(valid);
        auto wrongWidth = makeFrame(5, 0xff040506);
        wrongWidth->width = kDsScreenWidth / 2;
        auto wrongHeight = makeFrame(6, 0xff070809);
        wrongHeight->height = kDsScreenHeight / 2;

        box.publish(nullptr);
        box.publish(wrongWidth);
        box.publish(wrongHeight);

        const auto frame = box.latest();
        QVERIFY(frame);
        QCOMPARE(frame->sequence, std::uint64_t{4});
        QCOMPARE(frame->rgba.front(), std::uint32_t{0xff010203});
    }

    void heldSnapshotsStayImmutableAndSaturationDropsPublication() {
        FrameMailbox box;
        box.publish(makeFrame(1, 0xff111111));
        auto first = box.latest();
        box.publish(makeFrame(2, 0xff222222));
        auto second = box.latest();
        box.publish(makeFrame(3, 0xff333333));
        auto third = box.latest();

        box.publish(makeFrame(4, 0xff444444));

        QVERIFY(first);
        QVERIFY(second);
        QVERIFY(third);
        QCOMPARE(first->sequence, std::uint64_t{1});
        QCOMPARE(first->rgba.front(), std::uint32_t{0xff111111});
        QCOMPARE(second->sequence, std::uint64_t{2});
        QCOMPARE(second->rgba.front(), std::uint32_t{0xff222222});
        QCOMPARE(box.latest()->sequence, std::uint64_t{3});

        first.reset();
        box.publish(makeFrame(4, 0xff444444));

        QCOMPARE(box.latest()->sequence, std::uint64_t{4});
        QCOMPARE(second->sequence, std::uint64_t{2});
        QCOMPARE(second->rgba.front(), std::uint32_t{0xff222222});
    }
};

QTEST_GUILESS_MAIN(FrameMailboxTest)
#include "tst_frame_mailbox.moc"
