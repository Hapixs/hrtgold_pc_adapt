#pragma once

#include "core/input/InputRouter.h"
#include "core/rom/RomValidator.h"
#include "core/storage/AppPaths.h"

#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QUrl>
#include <QVariant>

#include <functional>

class UiState final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int sessionState READ sessionStateValue NOTIFY sessionStateChanged)
    Q_PROPERTY(bool menuVisible READ menuVisible NOTIFY sessionStateChanged)
    Q_PROPERTY(bool welcomeVisible READ welcomeVisible NOTIFY sessionStateChanged)
    Q_PROPERTY(int activeDevice READ activeDeviceValue NOTIFY activeDeviceChanged)
    Q_PROPERTY(int selectedTile READ selectedTile WRITE setSelectedTile NOTIFY selectedTileChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString bannerMessage READ bannerMessage NOTIFY bannerMessageChanged)
    Q_PROPERTY(bool blockingError READ blockingError NOTIFY blockingErrorChanged)
    Q_PROPERTY(QStringList tileNames READ tileNames CONSTANT)
    Q_PROPERTY(int volume READ volume NOTIFY optionsChanged)
    Q_PROPERTY(bool smoothScaling READ smoothScaling NOTIFY optionsChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY optionsChanged)
    Q_PROPERTY(bool reopenLastGame READ reopenLastGame NOTIFY optionsChanged)
    Q_PROPERTY(bool muteInBackground READ muteInBackground NOTIFY optionsChanged)
    Q_PROPERTY(bool optionsOpen READ optionsOpen NOTIFY optionsOpenChanged)
    Q_PROPERTY(int selectedOption READ selectedOption WRITE setSelectedOption NOTIFY selectedOptionChanged)
    Q_PROPERTY(QString footerConfirm READ footerConfirm NOTIFY activeDeviceChanged)
    Q_PROPERTY(QString footerBack READ footerBack NOTIFY activeDeviceChanged)
    Q_PROPERTY(QString footerMenu READ footerMenu NOTIFY activeDeviceChanged)

public:
    QString bannerMessage() const { return bannerMessage_; }
    bool blockingError() const { return blockingError_; }
    Q_INVOKABLE void dismissError();
    Q_INVOKABLE void stopSession();
    void reportEmulationError(const EmulationResult& error);
    using Validator = std::function<RomValidation(const std::filesystem::path&)>;
    using RomPicker = std::function<QString()>;
    using VolumeSetter = std::function<void(float)>;

    UiState(SessionController& session, InputRouter& router, const RomValidator& validator,
            const AppPaths& paths, QSettings& settings, RomPicker picker = {},
            VolumeSetter volumeSetter = {}, QObject* parent = nullptr);
    UiState(SessionController& session, InputRouter& router, Validator validator,
            const AppPaths& paths, QSettings& settings, RomPicker picker = {},
            VolumeSetter volumeSetter = {}, QObject* parent = nullptr);
    ~UiState() override;

    SessionState sessionState() const noexcept { return state_; }
    int sessionStateValue() const noexcept { return static_cast<int>(state_); }
    bool menuVisible() const noexcept { return blockingError_ || state_ == SessionState::MenuOpen; }
    bool welcomeVisible() const noexcept { return !blockingError_ && (state_ == SessionState::NoRom || state_ == SessionState::Error); }
    InputDeviceKind activeDevice() const noexcept { return device_; }
    int activeDeviceValue() const noexcept { return static_cast<int>(device_); }
    int selectedTile() const noexcept { return selectedTile_; }
    QString statusMessage() const { return statusMessage_; }
    QStringList tileNames() const;
    int volume() const noexcept { return volume_; }
    bool smoothScaling() const noexcept { return smoothScaling_; }
    bool fullscreen() const noexcept { return fullscreen_; }
    bool reopenLastGame() const noexcept { return reopenLastGame_; }
    bool muteInBackground() const noexcept { return muteInBackground_; }
    bool optionsOpen() const noexcept { return optionsOpen_; }
    int selectedOption() const noexcept { return selectedOption_; }
    QString footerConfirm() const;
    QString footerBack() const;
    QString footerMenu() const;

    Q_INVOKABLE void setSelectedTile(int tile);
    Q_INVOKABLE void activateSelected();
    Q_INVOKABLE void resumeGame();
    Q_INVOKABLE void chooseRom();
    Q_INVOKABLE void loadRomPath(const QString& path, bool rememberPath = true);
    Q_INVOKABLE void loadRomUrl(const QUrl& url);
    void restoreLastGame();
    Q_INVOKABLE void setOption(const QString& name, const QVariant& value);
    Q_INVOKABLE void setOptionValue(int option, const QVariant& value);
    Q_INVOKABLE void setSelectedOption(int option);
    Q_INVOKABLE void closeOptions();
    Q_INVOKABLE void handleKey(int key, bool pressed, bool repeated = false);
    void handleInput(const InputEvent& event);
    void setActiveDevice(InputDeviceKind kind);
    void setApplicationActive(bool active);

signals:
    void bannerMessageChanged();
    void blockingErrorChanged();
    void errorOccurred(int code);
    void privateFilesRequested();
    void romSelectionRequested();
    void sessionStateChanged();
    void activeDeviceChanged();
    void selectedTileChanged();
    void statusMessageChanged();
    void optionsChanged();
    void optionsOpenChanged();
    void selectedOptionChanged();

private:
    bool prepareRomSelection();
    void onSessionState(SessionState state);
    void setStatusMessage(QString message);
    void moveSelection(UiCommand command);
    void handleOptionCommand(UiCommand command);
    void updateEffectiveVolume();

    SessionController& session_;
    InputRouter& router_;
    Validator validator_;
    const AppPaths& paths_;
    QSettings& settings_;
    RomPicker picker_;
    VolumeSetter volumeSetter_;
    SessionState state_;
    InputDeviceKind device_{InputDeviceKind::Keyboard};
    int selectedTile_{0};
    QString statusMessage_;
    QString bannerMessage_;
    bool blockingError_{false};
    bool handlingError_{false};
    bool recoveryPending_{false};
    unsigned long long recoveryGeneration_{0};
    int volume_{100};
    bool smoothScaling_{false};
    bool fullscreen_{false};
    bool reopenLastGame_{false};
    bool muteInBackground_{false};
    bool applicationActive_{true};
    bool optionsOpen_{false};
    int selectedOption_{0};
    QString currentRomPath_;
};
