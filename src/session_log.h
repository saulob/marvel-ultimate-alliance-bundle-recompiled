#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace mua {

// Registers the clean-exit finalizer once. With Detailed logging off, a normal process exit then
// discards this session's log; with it on, the log is kept. Call after logging is initialized.
void InstallSessionLogCleanup();

// Discards this session's log when Detailed logging is off. Idempotent and never deletes a log that
// already contains a CRASH REPORT. Call only on the normal exit through atexit, when no thread can
// still log.
void FinalizeSessionLog();

// The same policy for the window close, where guest threads still log: logging stays up, and on
// Windows, where the open log cannot be deleted, the next start removes it.
void DiscardSessionLogBeforeHardExit();
void RemoveDiscardedSessionLogs();

// The launcher and the game share one normal log. A relaunch hands the active log to the next
// process: Begin sets the inherited path, Cancel clears it when the relaunch fails, and the child
// takes it once the previous process has exited.
void BeginSessionLogHandoff();
void CancelSessionLogHandoff();

// Takes the inherited path only when it is a session log inside one of `allowed_folders`; anything
// else, including a path supplied by an unrelated process, is ignored.
std::optional<std::filesystem::path> TakeInheritedSessionLog(
    const std::vector<std::filesystem::path>& allowed_folders);

}  // namespace mua
