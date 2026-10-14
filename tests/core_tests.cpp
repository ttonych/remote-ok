#include "../src/hid.hpp"
#include "../src/policy.hpp"
#include "../src/session.hpp"
#include <limits>

int checks = 0, failures = 0;
void Check(bool ok, const char* name) {
    ++checks; if (!ok) ++failures;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}
int main() {
    hid::Device d;
    const BYTE down[]{1, 0x41, 0}, up[]{1, 0, 0}, other[]{1, 0x40, 0}, id[]{2, 0x41, 0}, tail[]{1, 0x41, 1}, longer[]{1, 0x41, 0, 0};
    Check(hid::Decode(d, down, 3) == 1 && hid::Decode(d, up, 3) == 0, "known G60S down and release reports");
    Check(hid::Decode(d, other, 3) == -1 && hid::Decode(d, id, 3) == -1, "unrelated buttons and report IDs ignored");
    Check(hid::Decode(d, tail, 3) == -1 && hid::Decode(d, longer, 4) == -1, "fallback does not substring-match reports");
    Check(hid::Decode(d, nullptr, 0) == -1 && hid::Decode(d, down, 1) == -1, "empty and short reports rejected");
    hid::Edge edge;
    Check(!edge.update(false) && edge.update(true) && !edge.update(true) && !edge.update(false) && edge.update(true), "one event per press; holding does not repeat");
    Check(hid::ValidReports(6, 3, 2) && !hid::ValidReports(5, 3, 2), "report count is bounded by the buffer");
    Check(!hid::ValidReports(10, 0, 2) && !hid::ValidReports(10, 2, 0) && !hid::ValidReports(10, 2, MAXDWORD), "zero sizes and overflow counts rejected");
    using namespace routing;
    Context c; c.foreground = true;
    Check(Decide(c) == Result::Enter, "all-app mode sends Enter");
    c.mode = Mode::MpcOnly;
    Check(Decide(c) == Result::Ignore, "MPC-only mode ignores other applications");
    c.mpcProcess = c.mpcMainWindow = true;
    Check(Decide(c) == Result::Enter, "MPC default action is Enter");
    c.action = Action::PlayPause;
    Check(Decide(c) == Result::PlayPause, "MPC playback action");
    c.action = Action::Fullscreen;
    Check(Decide(c) == Result::Fullscreen, "MPC fullscreen action");
    c.mpcMainWindow = false;
    Check(Decide(c) == Result::Enter, "player dialogs retain Enter");
    c.kodiReserved = true;
    Check(Decide(c) == Result::KodiOwnsInput, "Kodi priority suppresses desktop handling");
    c.kodiSession = c.kodiAlive = c.foregroundIsKodi = true;
    Check(Decide(c) == Result::Enter, "Kodi helper acts inside its own Kodi process");
    c.foregroundIsKodi = false;
    Check(Decide(c) == Result::Ignore, "Kodi helper ignores another process");
    c.foregroundIsKodi = true; c.kodiAlive = false;
    Check(Decide(c) == Result::Ignore, "closed Kodi cannot receive input");
    c.kodiAlive = c.ownProcess = true;
    Check(Decide(c) == Result::Ignore, "configuration window never receives synthetic OK");
    c.ownProcess = false; c.foreground = false;
    Check(Decide(c) == Result::Ignore, "no foreground means no input");
    Check(IsMpcExecutable(L"C:\\Program Files\\MPC-HC\\mpc-hc64.exe") && IsMpcExecutable(L"MPC-BE.EXE"), "exact player names; case insensitive");
    Check(!IsMpcExecutable(L"fake-mpc-hc64.exe") && !IsMpcExecutable(L"mpc-be64.exe.bak"), "player name substrings rejected");
    const std::wstring base = L"Local\\G60S_OK_Test_" + std::to_wstring(GetCurrentProcessId());
    {
        session::Handle old(CreateMutexW(nullptr, FALSE, (base + L"_Native_1").c_str()));
        session::Instance candidate(base);
        Check(candidate.Acquire(0) == 3, "old incompatible helper blocks a new helper");
    }
    {
        session::Instance desktop(base);
        Check(desktop.Acquire(0) == 0, "desktop singleton acquired");
        {
            session::Instance duplicate(base);
            Check(duplicate.Acquire(0) == 3, "duplicate desktop refused");
        }
        {
            session::Instance kodi(base);
            Check(kodi.Acquire(GetCurrentProcessId()) == 0, "Kodi and desktop coexist");
            Check(session::KodiHasPriority(GetCurrentProcessId(), base), "Kodi priority visible to desktop");
            session::Instance duplicate(base);
            Check(duplicate.Acquire(GetCurrentProcessId()) == 3, "duplicate Kodi helper refused");
        }
        Check(!session::KodiHasPriority(GetCurrentProcessId(), base), "Kodi shutdown releases its priority");
    }
    {
        session::Handle old(OpenMutexW(SYNCHRONIZE, FALSE, (base + L"_Native_1").c_str()));
        Check(!old.value, "all instance handles released");
    }
    printf("%d checks, %d failures. No input was injected.\n", checks, failures);
    return failures ? 1 : 0;
}
