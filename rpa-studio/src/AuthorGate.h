#pragma once

#include <QString>

class QWidget;

namespace rpa::studio {

/// Gates flow authoring behind a password, so a copy installed at a customer's
/// site runs the flows it was given but cannot be used to write new ones.
///
/// What this is: a lock on the editor's front door. What it is not: protection
/// of the flows themselves. `.rpa.json` is plain text, `rpa-cli run` executes
/// any file it is handed, and anyone holding the executable can patch a
/// comparison out of it. Treat this as the difference between "a customer's
/// staff cannot casually build their own automations with our tool" and "our
/// tool cannot be used without permission" -- it delivers the first, and
/// nothing delivers the second short of tying a licence to each machine.
///
/// The password is never stored, compiled in, or written to settings. Only a
/// salted SHA-256 of it is compared, so `strings` on the binary does not hand
/// the password over.
class AuthorGate {
public:
    /// True when this install may create, edit, record or save flows.
    static bool unlocked();

    /// Ask for the password and unlock on success. Returns the resulting state,
    /// so a caller can abandon the action it was about to take.
    ///
    /// `reason` names the action that triggered the prompt, so the dialog says
    /// why it is asking rather than demanding a password out of nowhere.
    static bool promptToUnlock(QWidget* parent, const QString& reason);

    /// Drop back to run-only. Offered so an author can hand a machine over
    /// without reinstalling.
    static void lock();

    /// Convenience for action handlers: allow when unlocked, otherwise prompt,
    /// and report whether to go ahead.
    static bool require(QWidget* parent, const QString& reason);
};

}  // namespace rpa::studio
