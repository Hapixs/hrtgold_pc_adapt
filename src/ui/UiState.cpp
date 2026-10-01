#include "ui/UiState.h"

#include <Qt>
#include <QScopedValueRollback>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

namespace {
QString statusFor(RomStatus status) {
    if (status == RomStatus::UnsupportedGame) {
        return QStringLiteral("Ce prototype accepte uniquement Pokémon HeartGold.");
    }
    return QStringLiteral("Ce fichier n’est pas une ROM HeartGold valide.");
}
}

UiState::UiState(SessionController& session, InputRouter& router,
                 const RomValidator& validator, const AppPaths& paths,
                 QSettings& settings, RomPicker picker, VolumeSetter volumeSetter,
                 QObject* parent)
    : UiState(session, router, [&validator](const std::filesystem::path& path) {
          return validator.validate(path);
      }, paths, settings, std::move(picker), std::move(volumeSetter), parent) {}

UiState::UiState(SessionController& session, InputRouter& router, Validator validator,
                 const AppPaths& paths, QSettings& settings, RomPicker picker,
                 VolumeSetter volumeSetter, QObject* parent)
    : QObject(parent), session_(session), router_(router), validator_(std::move(validator)),
      paths_(paths), settings_(settings), picker_(std::move(picker)),
      volumeSetter_(std::move(volumeSetter)), state_(session.state()) {
    const auto storedVolume = settings_.value(QStringLiteral("volume"), 100).toInt();
    if (storedVolume >= 0 && storedVolume <= 100) volume_ = storedVolume;
    smoothScaling_ = settings_.value(QStringLiteral("smoothScaling"), false).toBool();
    fullscreen_ = settings_.value(QStringLiteral("fullscreen"), false).toBool();
    reopenLastGame_ = settings_.value(QStringLiteral("reopenLastGame"), false).toBool();
    muteInBackground_ = settings_.value(QStringLiteral("muteInBackground"), false).toBool();
    if (!reopenLastGame_) settings_.remove(QStringLiteral("lastRomPath"));
    updateEffectiveVolume();
    router_.synchronizeSessionState(state_);
    session_.onStateChanged([this](SessionState state) { onSessionState(state); });
    session_.onError([this](const EmulationResult& error) { reportEmulationError(error); });
}

UiState::~UiState() {
    session_.onStateChanged({});
    session_.onError({});
}

void UiState::reportEmulationError(const EmulationResult& error) {
    if (error.ok() || handlingError_) return;
    QScopedValueRollback guard(handlingError_, true);
    emit errorOccurred(static_cast<int>(error.code));
    if (error.code == EmulationErrorCode::AudioUnavailable) {
        bannerMessage_ = QStringLiteral("Audio indisponible — la partie continue sans son.");
        emit bannerMessageChanged();
        return;
    }
    if (error.code == EmulationErrorCode::SaveIo) {
        // If the worker already stopped on an I/O failure, openMenu reports
        // Error. Keep its last frame and block all input until acknowledged.
        if (state_ == SessionState::Playing) session_.openMenu();
        blockingError_ = true;
        closeOptions();
        setStatusMessage(QStringLiteral("La sauvegarde ne peut pas être protégée. La partie est en pause."));
        emit blockingErrorChanged();
        emit sessionStateChanged();
        return;
    }
    if (error.code == EmulationErrorCode::InvalidRom)
        setStatusMessage(QStringLiteral("Ce fichier n’est pas une ROM HeartGold valide."));
    else if (error.code == EmulationErrorCode::MissingPrivateFile)
        setStatusMessage(QStringLiteral("Sélectionnez vos fichiers système DS pour utiliser ce mode."));
    else
        setStatusMessage(QStringLiteral("L’émulation s’est arrêtée. Un journal local a été conservé."));
    if (recoveryPending_) return;
    recoveryPending_ = true;
    QTimer::singleShot(0, this, [this, code = error.code, generation = recoveryGeneration_] {
        if (generation != recoveryGeneration_) return;
        recoveryPending_ = false;
        // A new user selection may already have recovered before this callback.
        if (state_ == SessionState::Error || state_ == SessionState::Playing || state_ == SessionState::MenuOpen)
            stopSession();
        if (code == EmulationErrorCode::MissingPrivateFile) emit privateFilesRequested();
    });
}

void UiState::dismissError() {
    if (!blockingError_) return;
    blockingError_ = false;
    emit blockingErrorChanged();
    emit sessionStateChanged();
    // An adapter that has already shut down cannot safely resume; acknowledge
    // and return to the chooser. A live paused adapter remains in its menu.
    if (state_ == SessionState::Error) stopSession();
}

void UiState::stopSession() {
    if (state_ == SessionState::NoRom || state_ == SessionState::Loading || state_ == SessionState::Stopping) return;
    QScopedValueRollback guard(handlingError_, true);
    const auto result = session_.stop();
    if (!result.ok()) {
        emit errorOccurred(static_cast<int>(result.code));
        setStatusMessage(QStringLiteral("L’émulation s’est arrêtée. Un journal local a été conservé."));
    }
}

QStringList UiState::tileNames() const {
    return {QStringLiteral("Pokédex"), QStringLiteral("Équipe"),
            QStringLiteral("Sac"), QStringLiteral("Dresseur"),
            QStringLiteral("Pokégear"), QStringLiteral("Carte"),
            QStringLiteral("Options"), QStringLiteral("Reprendre")};
}

QString UiState::footerConfirm() const {
    switch (device_) {
    case InputDeviceKind::Xbox: return QStringLiteral("A  Confirmer");
    case InputDeviceKind::PlayStation: return QStringLiteral("Croix  Confirmer");
    case InputDeviceKind::GenericGamepad: return QStringLiteral("Bouton sud  Confirmer");
    case InputDeviceKind::Keyboard: return QStringLiteral("Entrée / E  Confirmer");
    }
    return {};
}

QString UiState::footerBack() const {
    switch (device_) {
    case InputDeviceKind::Xbox: return QStringLiteral("B  Retour");
    case InputDeviceKind::PlayStation: return QStringLiteral("Rond  Retour");
    case InputDeviceKind::GenericGamepad: return QStringLiteral("Bouton est  Retour");
    case InputDeviceKind::Keyboard: return QStringLiteral("Échap  Retour");
    }
    return {};
}

QString UiState::footerMenu() const {
    return device_ == InputDeviceKind::Keyboard ? QStringLiteral("Tab  Menu")
                                                : QStringLiteral("Start  Menu");
}

void UiState::setSelectedTile(int tile) {
    if (blockingError_) return;
    if (tile < 0 || tile >= 8 || selectedTile_ == tile) return;
    selectedTile_ = tile;
    setStatusMessage({});
    emit selectedTileChanged();
}

void UiState::activateSelected() {
    if (blockingError_) return;
    if (state_ != SessionState::MenuOpen || optionsOpen_) return;
    if (selectedTile_ < 6) {
        setStatusMessage(QStringLiteral("Disponible dans une prochaine étape"));
    } else if (selectedTile_ == 6) {
        optionsOpen_ = true;
        setSelectedOption(0);
        setStatusMessage({});
        emit optionsOpenChanged();
    } else {
        resumeGame();
    }
}

void UiState::resumeGame() {
    if (blockingError_) return;
    if (optionsOpen_) {
        closeOptions();
        return;
    }
    if (state_ == SessionState::MenuOpen) {
        session_.closeMenu();
    }
}

void UiState::chooseRom() {
    if (!prepareRomSelection()) return;
    if (picker_) loadRomPath(picker_());
    else emit romSelectionRequested();
}

void UiState::loadRomPath(const QString& selectedPath, bool rememberPath) {
    if (selectedPath.isEmpty()) return;
    if (!prepareRomSelection()) return;
    const auto romPath = std::filesystem::path(selectedPath.toStdWString());
    const auto validation = validator_(romPath);
    if (validation.status != RomStatus::ValidHeartGold) {
        setStatusMessage(statusFor(validation.status));
        return;
    }
    const auto saveDirectory = paths_.saveDirectory(validation.sha256);
    if (saveDirectory.empty()) {
        setStatusMessage(statusFor(RomStatus::MalformedHeader));
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(saveDirectory, error);
    if (error) {
        reportEmulationError({EmulationErrorCode::SaveIo, {}});
        return;
    }
    EmulationConfig config{romPath, saveDirectory / "game.sav", validation.sha256};
    const auto result = session_.load(config);
    if (!result.ok()) {
        return;
    }
    if (reopenLastGame_ && rememberPath) settings_.setValue(QStringLiteral("lastRomPath"), selectedPath);
    else settings_.remove(QStringLiteral("lastRomPath"));
    currentRomPath_ = rememberPath ? selectedPath : QString{};
    setStatusMessage({});
}

bool UiState::prepareRomSelection() {
    if (blockingError_ || (state_ != SessionState::NoRom && state_ != SessionState::Error)) return false;
    if (state_ != SessionState::Error) return true;
    const auto result = session_.stop();
    if (!result.ok()) {
        return false;
    }
    setStatusMessage({});
    return true;
}

void UiState::loadRomUrl(const QUrl& url) {
    if (!prepareRomSelection()) return;
    if (!url.isLocalFile() || url.toLocalFile().isEmpty()) {
        setStatusMessage(QStringLiteral("Ce fichier n’est pas une ROM HeartGold valide."));
        return;
    }
    loadRomPath(url.toLocalFile());
}

void UiState::restoreLastGame() {
    if (!reopenLastGame_ || state_ != SessionState::NoRom) return;
    const auto path = settings_.value(QStringLiteral("lastRomPath")).toString();
    if (!path.isEmpty()) loadRomPath(path);
}

void UiState::setOption(const QString& name, const QVariant& value) {
    if (blockingError_) return;
    if (name == QLatin1String("volume")) {
        const auto type = value.metaType().id();
        if (type != QMetaType::Int && type != QMetaType::UInt &&
            type != QMetaType::LongLong && type != QMetaType::ULongLong &&
            type != QMetaType::Double && type != QMetaType::Float) return;
        bool ok = false;
        const double numeric = value.toDouble(&ok);
        if (!ok || !std::isfinite(numeric) || numeric < 0 || numeric > 100 ||
            std::floor(numeric) != numeric) return;
        const int next = static_cast<int>(numeric);
        volume_ = next;
        updateEffectiveVolume();
        settings_.setValue(name, volume_);
    } else if (name == QLatin1String("smoothScaling") ||
               name == QLatin1String("fullscreen") ||
               name == QLatin1String("reopenLastGame") ||
               name == QLatin1String("muteInBackground")) {
        if (value.metaType().id() != QMetaType::Bool) return;
        const bool next = value.toBool();
        if (name == QLatin1String("smoothScaling")) smoothScaling_ = next;
        else if (name == QLatin1String("fullscreen")) fullscreen_ = next;
        else if (name == QLatin1String("muteInBackground")) {
            muteInBackground_ = next;
            updateEffectiveVolume();
        } else {
            reopenLastGame_ = next;
            if (!next) settings_.remove(QStringLiteral("lastRomPath"));
            else if (!currentRomPath_.isEmpty())
                settings_.setValue(QStringLiteral("lastRomPath"), currentRomPath_);
        }
        settings_.setValue(name, next);
    } else {
        return;
    }
    emit optionsChanged();
}

void UiState::setOptionValue(int option, const QVariant& value) {
    static const QStringList names{QStringLiteral("volume"),
                                   QStringLiteral("smoothScaling"),
                                   QStringLiteral("fullscreen"),
                                   QStringLiteral("reopenLastGame"),
                                   QStringLiteral("muteInBackground")};
    if (option < 0 || option >= names.size()) return;
    setSelectedOption(option);
    setOption(names.at(option), value);
}

void UiState::setSelectedOption(int option) {
    if (option < 0 || option > 5 || selectedOption_ == option) return;
    selectedOption_ = option;
    emit selectedOptionChanged();
}

void UiState::closeOptions() {
    if (!optionsOpen_) return;
    optionsOpen_ = false;
    emit optionsOpenChanged();
}

void UiState::handleKey(int key, bool pressed, bool repeated) {
    if (pressed) setActiveDevice(InputDeviceKind::Keyboard);
    InputAction action;
    switch (key) {
    case Qt::Key_Up: case Qt::Key_W: action = InputAction::MoveUp; break;
    case Qt::Key_Down: case Qt::Key_S: action = InputAction::MoveDown; break;
    case Qt::Key_Left: case Qt::Key_A: action = InputAction::MoveLeft; break;
    case Qt::Key_Right: case Qt::Key_D: action = InputAction::MoveRight; break;
    case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_E: action = InputAction::Confirm; break;
    case Qt::Key_Escape: action = InputAction::Back; break;
    case Qt::Key_Tab: action = InputAction::OpenMenu; break;
    case Qt::Key_F11: action = InputAction::ToggleFullscreen; break;
    default: return;
    }
    handleInput({action, pressed ? InputPhase::Pressed : InputPhase::Released,
                 InputDeviceKind::Keyboard, repeated});
}

void UiState::handleInput(const InputEvent& event) {
    if (!applicationActive_) return;
    if (blockingError_) {
        if (event.action == InputAction::Confirm && event.phase == InputPhase::Pressed && !event.repeated)
            dismissError();
        return;
    }
    if (event.phase == InputPhase::Pressed) setActiveDevice(event.device);
    const auto decision = router_.route(event, state_);
    if (decision.target == RouteTarget::Emulator) {
        session_.setDsButtons(decision.dsMask);
    } else if (decision.target == RouteTarget::Application) {
        if (decision.command == UiCommand::OpenMenu) session_.openMenu();
        else if (decision.command == UiCommand::CloseMenu) resumeGame();
        else if (decision.command == UiCommand::ChooseRom) chooseRom();
        else if (decision.command == UiCommand::ToggleFullscreen)
            setOption(QStringLiteral("fullscreen"), !fullscreen_);
    } else if (decision.target == RouteTarget::Overlay) {
        if (optionsOpen_) {
            handleOptionCommand(decision.command);
            return;
        }
        if (decision.command == UiCommand::Confirm) activateSelected();
        else if (decision.command == UiCommand::Back) resumeGame();
        else moveSelection(decision.command);
    }
}

void UiState::handleOptionCommand(UiCommand command) {
    if (command == UiCommand::Back) {
        closeOptions();
        return;
    }
    if (command == UiCommand::MoveUp) {
        setSelectedOption((selectedOption_ + 5) % 6);
    } else if (command == UiCommand::MoveDown) {
        setSelectedOption((selectedOption_ + 1) % 6);
    } else if (selectedOption_ == 0 &&
               (command == UiCommand::MoveLeft || command == UiCommand::MoveRight ||
                command == UiCommand::Confirm)) {
        const int delta = command == UiCommand::MoveLeft ? -5 : 5;
        setOption(QStringLiteral("volume"), std::clamp(volume_ + delta, 0, 100));
    } else if (selectedOption_ >= 1 && selectedOption_ <= 4) {
        const auto name = selectedOption_ == 1 ? QStringLiteral("smoothScaling") :
                          selectedOption_ == 2 ? QStringLiteral("fullscreen") :
                          selectedOption_ == 3 ? QStringLiteral("reopenLastGame") :
                                                 QStringLiteral("muteInBackground");
        const bool current = selectedOption_ == 1 ? smoothScaling_ :
                             selectedOption_ == 2 ? fullscreen_ :
                             selectedOption_ == 3 ? reopenLastGame_ : muteInBackground_;
        if (command == UiCommand::Confirm) setOption(name, !current);
        else if (command == UiCommand::MoveLeft) setOption(name, false);
        else if (command == UiCommand::MoveRight) setOption(name, true);
    } else if (selectedOption_ == 5 && command == UiCommand::Confirm) {
        closeOptions();
    }
}

void UiState::setApplicationActive(bool active) {
    applicationActive_ = active;
    if (!active) {
        router_.clearHeldButtons();
        session_.setDsButtons(0);
    }
    updateEffectiveVolume();
}

void UiState::updateEffectiveVolume() {
    if (volumeSetter_)
        volumeSetter_(muteInBackground_ && !applicationActive_ ? 0.0f : volume_ / 100.0f);
}

void UiState::setActiveDevice(InputDeviceKind kind) {
    if (device_ == kind) return;
    device_ = kind;
    emit activeDeviceChanged();
}

void UiState::onSessionState(SessionState state) {
    if (state == SessionState::Loading) {
        ++recoveryGeneration_;
        recoveryPending_ = false;
    }
    router_.synchronizeSessionState(state);
    state_ = state;
    if (state != SessionState::MenuOpen) closeOptions();
    emit sessionStateChanged();
}

void UiState::setStatusMessage(QString message) {
    if (statusMessage_ == message) return;
    statusMessage_ = std::move(message);
    emit statusMessageChanged();
}

void UiState::moveSelection(UiCommand command) {
    int next = selectedTile_;
    switch (command) {
    case UiCommand::MoveLeft: next = selectedTile_ / 4 * 4 + (selectedTile_ + 3) % 4; break;
    case UiCommand::MoveRight: next = selectedTile_ / 4 * 4 + (selectedTile_ + 1) % 4; break;
    case UiCommand::MoveUp: case UiCommand::MoveDown: next = (selectedTile_ + 4) % 8; break;
    default: return;
    }
    setSelectedTile(next);
}
