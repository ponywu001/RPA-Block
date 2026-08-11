#pragma once

#include <string>

namespace rpa::core {

struct LaunchResult {
    bool ok = false;
    /// 0 when the shell satisfied the request without handing back a process --
    /// a document routed into an already-running editor, for instance.
    unsigned long processId = 0;
    std::string error;
};

/// Open an application, document, or URL the way Explorer would. Backed by
/// ShellExecuteEx on Windows, so `path` may be an .exe, a .lnk, a file whose
/// extension carries an association, or an http(s):// address. Environment
/// variables in `path` and `workingDirectory` are expanded first, because a
/// portable flow wants `%ProgramFiles%\App\app.exe` rather than a drive letter.
LaunchResult launchApplication(const std::string& path,
                               const std::string& arguments = {},
                               const std::string& workingDirectory = {});

}  // namespace rpa::core
