#include "core/storage/AppPaths.h"
#include "core/storage/SaveRepository.h"

#include <QtTest>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace {
const std::string validHash(64, 'a');

std::filesystem::path toPath(const QString& path) {
    return std::filesystem::path{path.toStdWString()};
}

void writeBytes(const std::filesystem::path& path,
                std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {});
}

class PermissionRestorer {
public:
    explicit PermissionRestorer(std::filesystem::path path)
        : path_(std::move(path)), permissions_(std::filesystem::status(path_).permissions()) {}

    ~PermissionRestorer() {
        std::error_code ignored;
        std::filesystem::permissions(path_, permissions_,
                                     std::filesystem::perm_options::replace, ignored);
    }

private:
    std::filesystem::path path_;
    std::filesystem::perms permissions_;
};

auto fixedClock() {
    using namespace std::chrono;
    return [] { return sys_days{2024y / January / 2} + 3h + 4min + 5s; };
}
} // namespace

class SaveRepositoryTest final : public QObject {
    Q_OBJECT

private slots:
    void createsFirstSaveWithoutAnExistingActiveFile() {
        QTemporaryDir temporary;
        AppPaths paths(toPath(temporary.path()));
        const auto directory = paths.saveDirectory(validHash);
        QVERIFY(std::filesystem::create_directories(directory));
        SaveRepository repository(directory, fixedClock());
        const std::vector<std::uint8_t> bytes{9, 8, 7};
        QCOMPARE(repository.replace(bytes), SaveWriteError::None);
        QCOMPARE(repository.load(), bytes);
        QVERIFY(!std::filesystem::exists(directory / "game.sav.tmp"));
        QCOMPARE(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator()), 1);
    }
    void derivesSaveDirectoryOnlyFromHash() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const std::string unsafeDisplayName = "..\\\xc3\xa9vasion.nds";
        AppPaths paths(toPath(temporaryDirectory.path()));

        const auto saveDir = paths.saveDirectory(validHash);

        QVERIFY(saveDir.string().find(unsafeDisplayName) == std::string::npos);
        QVERIFY(saveDir.string().starts_with(paths.root().string()));
        QCOMPARE(saveDir.filename().string(), validHash);
        QCOMPARE(saveDir.parent_path().filename().string(), std::string("saves"));
    }

    void rejectsInvalidDirectoryKeys() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        AppPaths paths(toPath(temporaryDirectory.path()));

        QVERIFY(paths.saveDirectory(std::string(63, 'a')).empty());
        QVERIFY(paths.saveDirectory(std::string(64, 'A')).empty());
        QVERIFY(paths.saveDirectory(std::string("../") + std::string(61, 'a')).empty());
    }

    void replacesSaveAndCreatesTimestampedBackup() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        AppPaths paths(toPath(temporaryDirectory.path()));
        const auto directory = paths.saveDirectory(validHash);
        QVERIFY(std::filesystem::create_directories(directory));
        const std::array<std::uint8_t, 3> oldBytes{1, 2, 3};
        const std::array<std::uint8_t, 3> newBytes{4, 5, 6};
        writeBytes(directory / "game.sav", oldBytes);
        SaveRepository repository(directory, fixedClock());

        const auto error = repository.replace(newBytes);

        QCOMPARE(error, SaveWriteError::None);
        QCOMPARE(repository.load(), std::vector<std::uint8_t>(newBytes.begin(), newBytes.end()));
        std::vector<std::filesystem::path> backups;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().filename().string().starts_with("game-") &&
                entry.path().extension() == ".bak") {
                backups.push_back(entry.path());
            }
        }
        QCOMPARE(backups.size(), std::size_t{1});
        QCOMPARE(backups.front().filename().string(),
                 std::string("game-20240102-030405.sav.bak"));
        QCOMPARE(readBytes(backups.front()),
                 std::vector<std::uint8_t>(oldBytes.begin(), oldBytes.end()));
    }

    void rejectsRepositoryDirectoryWithoutHash() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        const std::array<std::uint8_t, 1> bytes{1};
        SaveRepository repository(toPath(temporaryDirectory.path()), fixedClock());

        QCOMPARE(repository.replace(bytes), SaveWriteError::InvalidHash);
    }

    void permitsRepeatedWritesWithinOneTimestampSecond() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        AppPaths paths(toPath(temporaryDirectory.path()));
        const auto directory = paths.saveDirectory(validHash);
        QVERIFY(std::filesystem::create_directories(directory));
        const std::array<std::uint8_t, 1> originalBytes{1};
        const std::array<std::uint8_t, 1> firstBytes{2};
        const std::array<std::uint8_t, 1> secondBytes{3};
        writeBytes(directory / "game.sav", originalBytes);
        SaveRepository repository(directory, fixedClock());
        QCOMPARE(repository.replace(firstBytes), SaveWriteError::None);

        const auto error = repository.replace(secondBytes);

        QCOMPARE(error, SaveWriteError::None);
        QCOMPARE(repository.load(),
                 std::vector<std::uint8_t>(secondBytes.begin(), secondBytes.end()));
        QCOMPARE(readBytes(directory / "game-20240102-030405.sav.bak"),
                 std::vector<std::uint8_t>(firstBytes.begin(), firstBytes.end()));
    }

    void reportsReadOnlyDirectoryAsNotWritable() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        AppPaths paths(toPath(temporaryDirectory.path()));
        const auto directory = paths.saveDirectory(validHash);
        QVERIFY(std::filesystem::create_directories(directory));
        PermissionRestorer restorePermissions(directory);
        std::filesystem::permissions(
            directory,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
            std::filesystem::perm_options::replace);
        const std::array<std::uint8_t, 1> bytes{1};
        SaveRepository repository(directory, fixedClock());

        QCOMPARE(repository.replace(bytes), SaveWriteError::NotWritable);
    }

    void removesOnlyTemporaryFileAfterRenameFailure() {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());
        AppPaths paths(toPath(temporaryDirectory.path()));
        const auto directory = paths.saveDirectory(validHash);
        QVERIFY(std::filesystem::create_directories(directory / "game.sav"));
        const std::array<std::uint8_t, 1> bytes{7};
        SaveRepository repository(directory, fixedClock());

        QCOMPARE(repository.replace(bytes), SaveWriteError::RenameFailed);
        QVERIFY(std::filesystem::is_directory(directory / "game.sav"));
        QVERIFY(!std::filesystem::exists(directory / "game.sav.tmp"));
    }
};

QTEST_GUILESS_MAIN(SaveRepositoryTest)
#include "tst_save_repository.moc"
