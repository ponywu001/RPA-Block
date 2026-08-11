// Headless probe for the agent gateway.
//
// The client's value is the real transport, so this drives the very same
// AgentClient the desktop app uses rather than reimplementing a stream reader
// that would only prove itself right. `--dump-sse` records what the gateway
// actually sends; that recording is what turns a live check into an offline
// regression fixture.

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <cstdlib>
#include <string>
#include <vector>

#include "rpa/ai/AgentClient.h"
#include "rpa/ai/AgentTypes.h"
#include "rpa/ai/PromptBuilder.h"
#include "rpa/core/Script.h"
#include "rpa/core/ScriptIO.h"

namespace {

using namespace rpa;

QTextStream& out() {
    static QTextStream stream(stdout);
    return stream;
}

QTextStream& err() {
    static QTextStream stream(stderr);
    return stream;
}

void printUsage() {
    out() << "usage: rpa-ai-probe <test|chat> [options]\n\n"
             "  test                    round-trip the gateway with a trivial schema\n"
             "  chat \"<message>\"        send a real flow-authoring request\n\n"
             "options:\n"
             "  --gateway <url>         default https://agents.scfg.io/structured-multimodal-agent\n"
             "  --assistant <id>        graph id (default structured-multimodal-agent)\n"
             "  --api-key <key>         defaults to the RPA_AI_API_KEY environment variable\n"
             "  --token <jwt>           use bearer auth instead of an API key\n"
             "  --provider <name>       claude | gemini\n"
             "  --model <name>\n"
             "  --timeout <ms>          default 120000\n"
             "  --flow <file.rpa.json>  send this flow as the current editor context\n"
             "  --image <file.png>      attach a screenshot\n"
             "  --dump-sse <file>       write the raw response stream to disk\n"
             "  --message-file <file>   read the message from a UTF-8 file\n"
             "  --dry-run               print the assembled request and send nothing\n";
    out().flush();
}

/// Returns false and reports when the flag is present but its value is missing,
/// so a typo fails loudly instead of silently probing the default gateway.
bool takeValue(const QStringList& args, int& i, const QString& flag, QString& value) {
    if (i + 1 >= args.size()) {
        err() << "error: " << flag << " needs a value\n";
        err().flush();
        return false;
    }
    value = args[++i];
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        printUsage();
        return 2;
    }

    const QString command = args[1];
    if (command != QLatin1String("test") && command != QLatin1String("chat")) {
        printUsage();
        return 2;
    }

    ai::AgentSettings settings;
    if (const char* envKey = std::getenv("RPA_AI_API_KEY"); envKey && *envKey) {
        settings.apiKey = envKey;
    }

    QString message;
    QString messageFile;
    QString flowPath;
    QString imagePath;
    QString dumpPath;
    bool dryRun = false;

    for (int i = 2; i < args.size(); ++i) {
        const QString& arg = args[i];
        QString value;
        if (arg == QLatin1String("--gateway")) {
            if (!takeValue(args, i, arg, value)) return 2;
            settings.gatewayUrl = value.toStdString();
        } else if (arg == QLatin1String("--assistant")) {
            if (!takeValue(args, i, arg, value)) return 2;
            settings.assistantId = value.toStdString();
        } else if (arg == QLatin1String("--api-key")) {
            if (!takeValue(args, i, arg, value)) return 2;
            settings.apiKey = value.toStdString();
        } else if (arg == QLatin1String("--token")) {
            if (!takeValue(args, i, arg, value)) return 2;
            settings.accessToken = value.toStdString();
            settings.authMode = ai::AuthMode::Jwt;
        } else if (arg == QLatin1String("--provider")) {
            if (!takeValue(args, i, arg, value)) return 2;
            settings.provider = value.toStdString();
        } else if (arg == QLatin1String("--model")) {
            if (!takeValue(args, i, arg, value)) return 2;
            settings.model = value.toStdString();
        } else if (arg == QLatin1String("--timeout")) {
            if (!takeValue(args, i, arg, value)) return 2;
            settings.timeoutMs = value.toInt();
        } else if (arg == QLatin1String("--flow")) {
            if (!takeValue(args, i, arg, flowPath)) return 2;
        } else if (arg == QLatin1String("--image")) {
            if (!takeValue(args, i, arg, imagePath)) return 2;
        } else if (arg == QLatin1String("--dump-sse")) {
            if (!takeValue(args, i, arg, dumpPath)) return 2;
        } else if (arg == QLatin1String("--message-file")) {
            if (!takeValue(args, i, arg, messageFile)) return 2;
        } else if (arg == QLatin1String("--dry-run")) {
            dryRun = true;
        } else if (arg.startsWith(QLatin1String("--"))) {
            err() << "error: unknown option " << arg << "\n";
            err().flush();
            return 2;
        } else if (message.isEmpty()) {
            message = arg;
        } else {
            err() << "error: unexpected argument " << arg << "\n";
            err().flush();
            return 2;
        }
    }

    // A message read from a UTF-8 file is the reliable path for non-ASCII text:
    // what a console hands a process depends on the shell and the code page,
    // and a mangled prompt does not fail -- the agent just answers a question
    // nobody asked.
    if (!messageFile.isEmpty()) {
        QFile file(messageFile);
        if (!file.open(QIODevice::ReadOnly)) {
            err() << "error: could not read " << messageFile << "\n";
            err().flush();
            return 2;
        }
        message = QString::fromUtf8(file.readAll()).trimmed();
    }

    if (command == QLatin1String("chat") && message.isEmpty()) {
        err() << "error: chat needs a message\n";
        err().flush();
        return 2;
    }
    if (dryRun) {
        out() << "message (" << message.size() << " chars): " << message << "\n";
        out() << "utf-8 bytes: " << message.toUtf8().toHex(' ') << "\n";
        out().flush();
        return 0;
    }
    if (settings.apiKey.empty() && settings.accessToken.empty()) {
        err() << "error: no credentials. Pass --api-key/--token or set RPA_AI_API_KEY.\n";
        err().flush();
        return 2;
    }

    core::Script script;
    if (!flowPath.isEmpty()) {
        const core::ParseResult parsed = core::loadScriptFile(flowPath.toStdString());
        if (!parsed.ok) {
            err() << "error: could not read " << flowPath << ": "
                  << QString::fromStdString(parsed.error) << "\n";
            err().flush();
            return 2;
        }
        script = parsed.script;
    }

    std::vector<unsigned char> imageBytes;
    if (!imagePath.isEmpty()) {
        QFile image(imagePath);
        if (!image.open(QIODevice::ReadOnly)) {
            err() << "error: could not read " << imagePath << "\n";
            err().flush();
            return 2;
        }
        const QByteArray raw = image.readAll();
        imageBytes.assign(raw.constData(), raw.constData() + raw.size());
    }

    ai::AgentClient client;
    client.setSettings(settings);

    // Opened eagerly so a stream that dies mid-flight still leaves its bytes on
    // disk -- that partial recording is the whole point when framing is what
    // broke.
    auto dumpFile = std::make_shared<QFile>(dumpPath);
    if (!dumpPath.isEmpty()) {
        if (!dumpFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            err() << "error: could not write " << dumpPath << "\n";
            err().flush();
            return 2;
        }
        client.setRawStreamTap([dumpFile](const QByteArray& chunk) {
            dumpFile->write(chunk);
            dumpFile->flush();
        });
    }

    int exitCode = 1;

    QObject::connect(&client, &ai::AgentClient::progress, [](const QString& note) {
        out() << "  … " << note << "\n";
        out().flush();
    });

    QObject::connect(&client, &ai::AgentClient::connectionTested,
                     [&](bool ok, const QString& detail) {
                         out() << (ok ? "OK   " : "FAIL ") << detail << "\n";
                         out().flush();
                         exitCode = ok ? 0 : 1;
                         QCoreApplication::quit();
                     });

    QObject::connect(&client, &ai::AgentClient::failed, [&](const ai::AgentError& error) {
        err() << "FAIL " << QString::fromStdString(error.message);
        if (error.httpStatus > 0) err() << " (HTTP " << error.httpStatus << ")";
        err() << "\n";
        err().flush();
        exitCode = 1;
        QCoreApplication::quit();
    });

    QObject::connect(&client, &ai::AgentClient::finished, [&](const ai::AgentReply& reply) {
        out() << "\nreply:       " << QString::fromStdString(reply.reply) << "\n";
        out() << "has_script:  " << (reply.hasScript ? "true" : "false") << "\n";
        out() << "script_name: " << QString::fromStdString(reply.scriptName) << "\n";
        out() << "steps:       " << reply.steps.size() << "\n";
        for (const core::Step& step : reply.steps) {
            out() << "  - " << QString::fromStdString(step.id) << " ("
                  << QString::fromStdString(core::toString(step.type)) << ")\n";
        }
        if (!reply.stepIssues.empty()) {
            out() << "issues:      " << reply.stepIssues.size() << "\n";
            for (const std::string& issue : reply.stepIssues) {
                out() << "  ! " << QString::fromStdString(issue) << "\n";
            }
        }
        out() << "cost:        $" << reply.costUsd << "  (" << reply.inputTokens << " in / "
              << reply.outputTokens << " out)\n";
        out().flush();

        // Issues mean the draft is unusable as-is. The desktop app repairs it in
        // another round; here it is a finding, so it fails the check.
        exitCode = reply.stepIssues.empty() ? 0 : 1;
        QCoreApplication::quit();
    });

    // The transfer timeout only covers stalls between bytes; a gateway that
    // streams keep-alives forever would never trip it.
    QTimer wallClock;
    wallClock.setSingleShot(true);
    QObject::connect(&wallClock, &QTimer::timeout, [&]() {
        err() << "FAIL probe timed out after " << settings.timeoutMs << " ms\n";
        err().flush();
        client.cancel();
        exitCode = 1;
        QCoreApplication::quit();
    });
    wallClock.start(settings.timeoutMs + 5000);

    if (command == QLatin1String("test")) {
        out() << "probing " << QString::fromStdString(settings.gatewayUrl) << " …\n";
        out().flush();
        client.testConnection();
    } else {
        ai::ChatMessage turn;
        turn.role = ai::ChatMessage::Role::User;
        turn.text = ai::PromptBuilder::chatRequest(message.toStdString(), script);
        turn.imagePng = std::move(imageBytes);

        out() << "asking " << QString::fromStdString(settings.gatewayUrl) << " …\n";
        out().flush();
        client.send({turn});
    }

    app.exec();

    if (dumpFile->isOpen()) {
        dumpFile->close();
        out() << "raw stream written to " << dumpPath << "\n";
        out().flush();
    }
    return exitCode;
}
