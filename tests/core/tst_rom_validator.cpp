#include "core/rom/RomValidator.h"

#include <QtTest>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

class RomValidatorTest final : public QObject {
    Q_OBJECT

private:
    QTemporaryDir temporaryDirectory;
    int fixtureIndex = 0;

    static void writeLittleEndian32(std::vector<std::uint8_t>& bytes,
                                    std::size_t offset,
                                    std::uint32_t value) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            bytes[offset + byte] =
                static_cast<std::uint8_t>((value >> (byte * 8)) & 0xffU);
        }
    }

    std::filesystem::path writeFixture(
        const std::string& gameCode,
        std::uint32_t arm9Offset = 0x200,
        std::uint32_t arm9Size = 4,
        std::string title = "POKEMON HG") {
        std::vector<std::uint8_t> bytes(4096, 0);
        std::copy(title.begin(), title.end(), bytes.begin());
        std::copy(gameCode.begin(), gameCode.end(), bytes.begin() + 0x0c);
        writeLittleEndian32(bytes, 0x20, arm9Offset);
        writeLittleEndian32(bytes, 0x2c, arm9Size);

        const auto fileName = QStringLiteral("fixture-%1.nds").arg(fixtureIndex++);
        const auto path = std::filesystem::path{
            temporaryDirectory.filePath(fileName).toStdWString()};
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.close();
        return path;
    }

private slots:
    void initTestCase() {
        QVERIFY(temporaryDirectory.isValid());
    }

    void acceptsHeartGoldHeader() {
        RomValidator validator;

        const auto result = validator.validate(writeFixture("IPKF"));

        QCOMPARE(result.status, RomStatus::ValidHeartGold);
        QCOMPARE(result.title, std::string("POKEMON HG"));
        QCOMPARE(result.gameCode, std::string("IPKF"));
        QCOMPARE(result.sha256.size(), size_t{64});
        QCOMPARE(result.size, std::uint64_t{4096});
    }

    void rejectsAnotherGame() {
        RomValidator validator;

        const auto result = validator.validate(writeFixture("ABCD"));

        QCOMPARE(result.status, RomStatus::UnsupportedGame);
    }

    void rejectsTruncatedArm9Range() {
        RomValidator validator;

        const auto result = validator.validate(writeFixture("IPKF", 0x5000, 4));

        QCOMPARE(result.status, RomStatus::MalformedHeader);
    }

    void reportsMissingFile() {
        RomValidator validator;
        const auto path = std::filesystem::path{
            temporaryDirectory.filePath(QStringLiteral("missing.nds")).toStdWString()};

        const auto result = validator.validate(path);

        QCOMPARE(result.status, RomStatus::FileNotFound);
    }

    void rejectsFileSmallerThanHeader() {
        const auto path = std::filesystem::path{
            temporaryDirectory.filePath(QStringLiteral("small.nds")).toStdWString()};
        std::ofstream output(path, std::ios::binary);
        const std::array<std::uint8_t, 0x1ff> bytes{};
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.close();
        RomValidator validator;

        const auto result = validator.validate(path);

        QCOMPARE(result.status, RomStatus::TooSmall);
    }

    void trimsTitleAtFirstNul() {
        RomValidator validator;

        const auto result = validator.validate(
            writeFixture("IPKE", 0x200, 4, std::string("HG\0JUNK", 7)));

        QCOMPARE(result.title, std::string("HG"));
    }

    void producesLowercaseSha256() {
        RomValidator validator;

        const auto result = validator.validate(writeFixture("IPKS"));

        QVERIFY(std::ranges::all_of(result.sha256, [](const char character) {
            return (character >= '0' && character <= '9') ||
                   (character >= 'a' && character <= 'f');
        }));
    }
};

QTEST_GUILESS_MAIN(RomValidatorTest)
#include "tst_rom_validator.moc"
