#pragma once
#include <string>
#include <cwctype>
#include <algorithm>

namespace routing {
enum class Mode { AllApps, MpcOnly };
enum class Action { Enter, PlayPause, Fullscreen };
enum class Result { Ignore, KodiOwnsInput, Enter, PlayPause, Fullscreen };
struct Context {
    bool foreground = false, ownProcess = false;
    bool kodiSession = false, kodiAlive = false, foregroundIsKodi = false;
    bool kodiReserved = false, mpcProcess = false, mpcMainWindow = false;
    Mode mode = Mode::AllApps;
    Action action = Action::Enter;
};
inline Result Decide(const Context& c) {
    if (!c.foreground || c.ownProcess) return Result::Ignore;
    if (c.kodiSession) return c.kodiAlive && c.foregroundIsKodi ? Result::Enter : Result::Ignore;
    if (c.kodiReserved) return Result::KodiOwnsInput;
    if (c.mode == Mode::MpcOnly && !c.mpcProcess) return Result::Ignore;
    if (c.mpcProcess && c.mpcMainWindow) {
        if (c.action == Action::PlayPause) return Result::PlayPause;
        if (c.action == Action::Fullscreen) return Result::Fullscreen;
    }
    return Result::Enter;
}
inline bool IsMpcExecutable(std::wstring name) {
    const auto slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name.erase(0, slash + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return name == L"mpc-hc.exe" || name == L"mpc-hc64.exe" || name == L"mpc-be.exe" || name == L"mpc-be64.exe";
}
// Identical command IDs in the upstream MPC-HC and MPC-BE resource.h files.
constexpr unsigned PlayPauseCommand = 889, FullscreenCommand = 830;
} // namespace routing
