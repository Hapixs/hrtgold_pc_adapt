#include "ui/UiState.h"

#include <Qt>

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
    : QObject(parent), session_(session), router_(router), validator_(validator),
      paths_(paths), settings_(settings), picker_(std::move(picker)),
      volumeSetter_(std::move(volumeSetter)), state_(session.state()) {
    const auto storedVolume = settings_.value(QStringLiteral("volume"), 100).toInt();
    if (storedVolume >= 0 && storedVolume <= 100) volume_ = storedVolume;
    smoothScaling_ = settings_.value(QStringLiteral("smoothScaling"), false).toBool();
    fullscreen_ = settings_.value(QStringLiteral("fullscreen"), false).toBool();
    reopenLastGame_ = settings_.value(QStringLiteral("reopenLastGame"), false).toBool();
    if (!reopenLastGame_) settings_.remove(QStringLiteral("lastRomPath"));
    if (volumeSetter_) volumeSetter_(volume_ / 100.0f);
    router_.synchronizeSessionState(state_);
    session_.onStateChanged([this](SessionState state) { onSessionState(state); });
    session_.onError([this](const EmulationResult&) {
        setStatusMessage(QStringLiteral("L’émulation s’est arrêtée. Un journal local a été conservé."));
    });
}

UiState::~UiState() {
    session_.onStateChanged({});
    session_.onError({});
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
    if (tile < 0 || tile >= 8 || selectedTile_ == tile) return;
    selectedTile_ = tile;
    setStatusMessage({});
    emit selectedTileChanged();
}

void UiState::activateSelected() {
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
    if (optionsOpen_) {
        closeOptions();
        return;
    }
    if (state_ == SessionState::MenuOpen) {
        const auto result = session_.closeMenu();
        if (!result.ok()) setStatusMessage(QString::fromStdString(result.message));
    }
}

void UiState::chooseRom() {
    if (picker_) loadRomPath(picker_());
    else emit romSelectionRequested();
}

void UiState::loadRomPath(const QString& selectedPath) {
    if (selectedPath.isEmpty()) return;
    const auto romPath = std::filesystem::path(selectedPath.toStdWString());
    const auto validation = validator_.validate(romPath);
    if (validation.status != RomStatus::ValidHeartGold) {
        setStatusMessage(statusFor(validation.status));
        return;
    }
    const auto saveDirectory = paths_.saveDirectory(validation.sha256);
    std::error_code error;
    std::filesystem::create_directories(saveDirectory, error);
    if (error) {
        setStatusMessage(QStringLiteral("La sauvegarde ne peut pas être protégée. La partie est en pause."));
        return;
    }
    EmulationConfig config{romPath, saveDirectory / "native.sav", validation.sha256};
    const auto result = session_.load(config);
    if (!result.ok()) {
        setStatusMessage(QString::fromStdString(result.message));
        return;
    }
    if (reopenLastGame_) settings_.setValue(QStringLiteral("lastRomPath"), selectedPath);
    else settings_.remove(QStringLiteral("lastRomPath"));
    currentRomPath_ = selectedPath;
    setStatusMessage({});
}

void UiState::loadRomUrl(const QUrl& url) {
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
        if (volumeSetter_) volumeSetter_(volume_ / 100.0f);
        settings_.setValue(name, volume_);
    } else if (name == QLatin1String("smoothScaling") ||
               name == QLatin1String("fullscreen") ||
               name == QLatin1String("reopenLastGame")) {
        if (value.metaType().id() != QMetaType::Bool) return;
        const bool next = value.toBool();
        if (name == QLatin1String("smoothScaling")) smoothScaling_ = next;
        else if (name == QLatin1String("fullscreen")) fullscreen_ = next;
        else {
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
                                   QStringLiteral("reopenLastGame")};
    if (option < 0 || option >= names.size()) return;
    setSelectedOption(option);
    setOption(names.at(option), value);
}

void UiState::setSelectedOption(int option) {
    if (option < 0 || option > 4 || selectedOption_ == option) return;
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
        setSelectedOption((selectedOption_ + 4) % 5);
    } else if (command == UiCommand::MoveDown) {
        setSelectedOption((selectedOption_ + 1) % 5);
    } else if (selectedOption_ == 0 &&
               (command == UiCommand::MoveLeft || command == UiCommand::MoveRight ||
                command == UiCommand::Confirm)) {
        const int delta = command == UiCommand::MoveLeft ? -5 : 5;
        setOption(QStringLiteral("volume"), std::clamp(volume_ + delta, 0, 100));
    } else if (selectedOption_ >= 1 && selectedOption_ <= 3) {
        const auto name = selectedOption_ == 1 ? QStringLiteral("smoothScaling") :
                          selectedOption_ == 2 ? QStringLiteral("fullscreen") :
                                                 QStringLiteral("reopenLastGame");
        const bool current = selectedOption_ == 1 ? smoothScaling_ :
                             selectedOption_ == 2 ? fullscreen_ : reopenLastGame_;
        if (command == UiCommand::Confirm) setOption(name, !current);
        else if (command == UiCommand::MoveLeft) setOption(name, false);
        else if (command == UiCommand::MoveRight) setOption(name, true);
    } else if (selectedOption_ == 4 && command == UiCommand::Confirm) {
        closeOptions();
    }
}

void UiState::setActiveDevice(InputDeviceKind kind) {
    if (device_ == kind) return;
    device_ = kind;
    emit activeDeviceChanged();
}

void UiState::onSessionState(SessionState state) {
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
