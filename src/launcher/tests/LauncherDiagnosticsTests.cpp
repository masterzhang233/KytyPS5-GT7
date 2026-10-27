#include "launcherDiagnostics.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int failures = 0;

void Check(bool pass, const char* what) {
	if (!pass) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		++failures;
	}
}

bool WriteFile(const QString& path, const QByteArray& data) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray ReadFile(const QString& path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray {};
}

} // namespace

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	if (app.arguments().contains(QStringLiteral("--diagnostic-child"))) {
		std::fputs("diagnostic-child-stdout\n", stdout);
		std::fputs("diagnostic-child-stderr\n", stderr);
		return 0;
	}

	QTemporaryDir temporary;
	Check(temporary.isValid(), "temporary directory");
	const auto fake_emulator = temporary.filePath(QStringLiteral("fake-emulator.exe"));
	Check(WriteFile(fake_emulator, QByteArray("fake-emulator-build")), "fake emulator file");

	LauncherDiagnostics::RunDetails details;
	details.title_id             = QStringLiteral("PPSA01317");
	details.game_version         = QStringLiteral("01.710.000");
	details.output_resolution    = QStringLiteral("2560x1440");
	details.mode                 = QStringLiteral("SteamVR");
	details.vr_scale             = 120;
	details.gpu_index            = 0;
	details.emulator_path        = fake_emulator;
	const auto private_game_path = temporary.filePath(QStringLiteral("Private Games/GT7"));
	details.private_paths        = {private_game_path};
	QString error;
	auto    run = LauncherDiagnostics::Run::Create(details, &error,
	                                               temporary.filePath(QStringLiteral("logs")));
	Check(run != nullptr && error.isEmpty(), "create timestamped run directory");
	if (run == nullptr) {
		return 1;
	}
	Check(QFileInfo(run->Directory()).isDir(), "run directory exists");
	run->RecordPreflight(QByteArray("preflight ok\n"), {}, 0, true, {}, false);
	Check(ReadFile(QDir(run->Directory()).filePath(QStringLiteral("preflight.stdout.log"))) ==
	          QByteArray("preflight ok\n"),
	      "preflight output is captured in run directory");

	QProcess child;
	run->ConfigureProcess(&child);
	child.setProgram(app.applicationFilePath());
	child.setArguments({QStringLiteral("--diagnostic-child")});
	child.start();
	Check(child.waitForStarted(5000), "diagnostic child starts");
	run->RecordStarted(child.processId());
	Check(child.waitForFinished(5000), "diagnostic child finishes");
	run->RecordFinished(child.exitCode(), child.exitStatus() == QProcess::NormalExit);
	Check(ReadFile(QDir(run->Directory()).filePath(QStringLiteral("emulator.stdout.log")))
	          .contains("diagnostic-child-stdout"),
	      "stdout is captured in run directory");
	Check(ReadFile(QDir(run->Directory()).filePath(QStringLiteral("emulator.stderr.log")))
	          .contains("diagnostic-child-stderr"),
	      "stderr is captured in run directory");
	Check(WriteFile(run->EmulatorLogPath(), QByteArray("main emulator diagnostic at ") +
	                                            private_game_path.toUtf8() +
	                                            QByteArray("/private-location\n")),
	      "main emulator log fixture");

	const auto summary_data =
	    ReadFile(QDir(run->Directory()).filePath(QStringLiteral("session.json")));
	const auto summary = QJsonDocument::fromJson(summary_data).object();
	Check(summary.value(QStringLiteral("status")).toString() == QStringLiteral("finished"),
	      "summary records successful exit");
	Check(summary.value(QStringLiteral("exit_code")).toInt(-1) == 0,
	      "summary records process exit code");
	Check(summary.value(QStringLiteral("build"))
	              .toObject()
	              .value(QStringLiteral("git_revision"))
	              .toString()
	              .size() == 40,
	      "summary records exact source revision");
	Check(!summary_data.contains(temporary.path().toUtf8()),
	      "summary omits installation and user paths");

	Check(WriteFile(QDir(run->Directory()).filePath(QStringLiteral("eboot.bin")),
	                QByteArray("SECRET_GAME_PAYLOAD")),
	      "excluded game file fixture");
	Check(QDir().mkpath(QDir(run->Directory()).filePath(QStringLiteral("_SaveData"))),
	      "excluded save directory fixture");
	Check(WriteFile(QDir(run->Directory()).filePath(QStringLiteral("_SaveData/user.dat")),
	                QByteArray("SECRET_SAVE_PAYLOAD")),
	      "excluded save file fixture");
	const auto zip_path = temporary.filePath(QStringLiteral("support.zip"));
	Check(LauncherDiagnostics::ExportRunBundle(run->Directory(), zip_path, &error),
	      "export diagnostic ZIP");
	const auto zip = ReadFile(zip_path);
	Check(zip.startsWith(QByteArray("PK\x03\x04", 4)) && zip.contains(QByteArray("PK\x05\x06", 4)),
	      "export is a complete ZIP archive");
	Check(zip.contains("session.json") && zip.contains("launcher.log") &&
	          zip.contains("emulator-main.log") && zip.contains("README.txt"),
	      "ZIP contains allow-listed diagnostics");
	Check(zip.contains("main emulator diagnostic") && zip.contains("diagnostic-child-stderr"),
	      "ZIP contains captured diagnostic text");
	Check(zip.contains("<redacted-path>/private-location") &&
	          !zip.contains(private_game_path.toUtf8()) && !zip.contains(".redact-paths"),
	      "ZIP redacts private paths and excludes its private redaction sidecar");
	Check(!zip.contains("eboot.bin") && !zip.contains("SECRET_GAME_PAYLOAD") &&
	          !zip.contains("_SaveData") && !zip.contains("SECRET_SAVE_PAYLOAD"),
	      "ZIP excludes game and save data");
	Check(LauncherDiagnostics::LatestRunDirectory(temporary.filePath(QStringLiteral("logs"))) ==
	          run->Directory(),
	      "latest run is found without reading launcher settings");
	run->RecordFinished(23, false);
	const auto crash_summary =
	    QJsonDocument::fromJson(
	        ReadFile(QDir(run->Directory()).filePath(QStringLiteral("session.json"))))
	        .object();
	Check(crash_summary.value(QStringLiteral("status")).toString() == QStringLiteral("crashed") &&
	          crash_summary.value(QStringLiteral("exit_code")).toInt() == 23,
	      "summary records crash status and exit code");

	std::printf("Launcher diagnostics tests: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
