#include "launcherDiagnostics.h"

#include "kytyGitVersion.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // IWYU pragma: keep
#endif

namespace LauncherDiagnostics {
namespace {

constexpr char   SummaryFile[]      = "session.json";
constexpr char   LauncherLogFile[]  = "launcher.log";
constexpr char   PreflightOutFile[] = "preflight.stdout.log";
constexpr char   PreflightErrFile[] = "preflight.stderr.log";
constexpr char   EmulatorOutFile[]  = "emulator.stdout.log";
constexpr char   EmulatorErrFile[]  = "emulator.stderr.log";
constexpr char   EmulatorMainFile[] = "emulator-main.log";
constexpr char   RedactionFile[]    = ".redact-paths";
constexpr qint64 SmallLogTail       = 2ll * 1024ll * 1024ll;
constexpr qint64 ProcessLogTail     = 8ll * 1024ll * 1024ll;
constexpr qint64 EmulatorLogTail    = 24ll * 1024ll * 1024ll;

QString UtcNow() {
	return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString CleanTitleId(QString value) {
	value = value.trimmed();
	for (auto& character: value) {
		if (!character.isLetterOrNumber() && character != QLatin1Char('-') &&
		    character != QLatin1Char('_')) {
			character = QLatin1Char('_');
		}
	}
	return value.isEmpty() ? QStringLiteral("GT7") : value.left(32);
}

QByteArray HashFile(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file)) {
		return {};
	}
	return hash.result().toHex();
}

QJsonObject HardwareSummary(int selected_gpu_index) {
	QJsonObject hardware;
	hardware.insert(QStringLiteral("os"), QSysInfo::prettyProductName());
	hardware.insert(QStringLiteral("kernel"),
	                QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion());
	hardware.insert(QStringLiteral("build_abi"), QSysInfo::buildAbi());
	hardware.insert(QStringLiteral("cpu_architecture"), QSysInfo::currentCpuArchitecture());
	hardware.insert(QStringLiteral("logical_processors"), QThread::idealThreadCount());
	hardware.insert(QStringLiteral("selected_gpu_index"), selected_gpu_index);

#if defined(_WIN32)
	const auto processor = qEnvironmentVariable("PROCESSOR_IDENTIFIER").trimmed();
	if (!processor.isEmpty()) {
		hardware.insert(QStringLiteral("processor"), processor);
	}
	MEMORYSTATUSEX memory {};
	memory.dwLength = sizeof(memory);
	if (GlobalMemoryStatusEx(&memory) != FALSE) {
		hardware.insert(QStringLiteral("physical_memory_mib"),
		                static_cast<qint64>(memory.ullTotalPhys / (1024ull * 1024ull)));
	}
	QJsonArray adapters;
	for (DWORD index = 0;; ++index) {
		DISPLAY_DEVICEW device {};
		device.cb = sizeof(device);
		if (EnumDisplayDevicesW(nullptr, index, &device, 0) == FALSE) {
			break;
		}
		if ((device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0) {
			continue;
		}
		const auto name = QString::fromWCharArray(device.DeviceString).trimmed();
		if (!name.isEmpty() && !adapters.contains(name)) {
			adapters.append(name);
		}
	}
	if (!adapters.isEmpty()) {
		hardware.insert(QStringLiteral("display_adapters"), adapters);
	}
#endif
	return hardware;
}

bool WriteAll(const QString& path, const QByteArray& data, QString* error) {
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		if (error != nullptr) {
			*error = file.errorString();
		}
		return false;
	}
	if (file.write(data) != data.size() || !file.commit()) {
		if (error != nullptr) {
			*error = file.errorString();
		}
		return false;
	}
	return true;
}

QByteArray ReadTail(const QString& path, qint64 limit, bool* truncated, QString* error) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error != nullptr) {
			*error = file.errorString();
		}
		return {};
	}
	const qint64 size = file.size();
	*truncated        = size > limit;
	if (*truncated && !file.seek(size - limit)) {
		if (error != nullptr) {
			*error = file.errorString();
		}
		return {};
	}
	auto data = file.readAll();
	if (*truncated) {
		// The first bytes may begin in the middle of a line (and therefore in the middle of a
		// private path). Drop that partial line before applying path redaction.
		const auto newline = data.indexOf('\n');
		data               = newline >= 0 ? data.mid(newline + 1) : QByteArray {};
		data.prepend(QByteArray("[Earlier content omitted from this support package; the original "
		                        "log remains in the local run directory.]\n"));
	}
	return data;
}

QStringList NormalizePrivatePaths(QStringList paths) {
	paths.append(QDir::homePath());
	paths.append(QDir::tempPath());
	QStringList normalized;
	for (const auto& path: paths) {
		if (path.trimmed().isEmpty()) {
			continue;
		}
		const auto absolute = QFileInfo(path).isAbsolute() ? QDir::cleanPath(path)
		                                                   : QFileInfo(path).absoluteFilePath();
		if (absolute.size() >= 3 && !normalized.contains(absolute, Qt::CaseInsensitive)) {
			normalized.append(absolute);
		}
	}
	std::sort(normalized.begin(), normalized.end(),
	          [](const QString& left, const QString& right) { return left.size() > right.size(); });
	return normalized;
}

QStringList LoadRedactionPaths(const QString& run_directory) {
	QStringList paths {QDir::homePath(), QDir::tempPath()};
	QFile       file(QDir(run_directory).absoluteFilePath(QString::fromLatin1(RedactionFile)));
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		while (!file.atEnd()) {
			const auto decoded = QByteArray::fromBase64(file.readLine().trimmed());
			if (!decoded.isEmpty()) {
				paths.append(QString::fromUtf8(decoded));
			}
		}
	}
	return NormalizePrivatePaths(std::move(paths));
}

QByteArray RedactPaths(const QByteArray& data, const QStringList& private_paths) {
	QString text = QString::fromUtf8(data);
	for (const auto& path: private_paths) {
		QStringList spellings {QDir::cleanPath(path), QDir::toNativeSeparators(path)};
		for (auto spelling: spellings) {
			if (spelling.isEmpty()) {
				continue;
			}
			text.replace(spelling, QStringLiteral("<redacted-path>"), Qt::CaseInsensitive);
			spelling.replace(QLatin1Char('\\'), QLatin1Char('/'));
			text.replace(spelling, QStringLiteral("<redacted-path>"), Qt::CaseInsensitive);
		}
	}
	return text.toUtf8();
}

void Put16(QIODevice* output, quint16 value) {
	const std::array<char, 2> bytes {static_cast<char>(value & 0xffu),
	                                 static_cast<char>((value >> 8u) & 0xffu)};
	output->write(bytes.data(), static_cast<qint64>(bytes.size()));
}

void Put32(QIODevice* output, quint32 value) {
	const std::array<char, 4> bytes {
	    static_cast<char>(value & 0xffu), static_cast<char>((value >> 8u) & 0xffu),
	    static_cast<char>((value >> 16u) & 0xffu), static_cast<char>((value >> 24u) & 0xffu)};
	output->write(bytes.data(), static_cast<qint64>(bytes.size()));
}

quint32 Crc32(const QByteArray& data) {
	quint32 crc = 0xffffffffu;
	for (const auto byte: data) {
		crc ^= static_cast<unsigned char>(byte);
		for (int bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

struct ZipEntry {
	QByteArray name;
	QByteArray data;
	quint32    crc          = 0;
	quint32    local_offset = 0;
};

bool WriteStoredZip(const QString& path, std::vector<ZipEntry>* entries, QString* error) {
	QSaveFile output(path);
	if (!output.open(QIODevice::WriteOnly)) {
		if (error != nullptr) {
			*error = output.errorString();
		}
		return false;
	}
	constexpr quint16 Utf8Flag = 1u << 11u;
	for (auto& entry: *entries) {
		if (entry.name.size() > std::numeric_limits<quint16>::max() ||
		    entry.data.size() > std::numeric_limits<quint32>::max() ||
		    output.pos() > std::numeric_limits<quint32>::max()) {
			if (error != nullptr) {
				*error = QStringLiteral("Diagnostic package is too large for ZIP32");
			}
			output.cancelWriting();
			return false;
		}
		entry.crc          = Crc32(entry.data);
		entry.local_offset = static_cast<quint32>(output.pos());
		Put32(&output, 0x04034b50u);
		Put16(&output, 20);
		Put16(&output, Utf8Flag);
		Put16(&output, 0); // Stored: the diagnostic tails are intentionally not recompressed.
		Put16(&output, 0);
		Put16(&output, 0);
		Put32(&output, entry.crc);
		Put32(&output, static_cast<quint32>(entry.data.size()));
		Put32(&output, static_cast<quint32>(entry.data.size()));
		Put16(&output, static_cast<quint16>(entry.name.size()));
		Put16(&output, 0);
		output.write(entry.name);
		output.write(entry.data);
	}
	if (output.pos() > std::numeric_limits<quint32>::max() ||
	    entries->size() > std::numeric_limits<quint16>::max()) {
		if (error != nullptr) {
			*error = QStringLiteral("Diagnostic package is too large for ZIP32");
		}
		output.cancelWriting();
		return false;
	}
	const auto central_offset = static_cast<quint32>(output.pos());
	for (const auto& entry: *entries) {
		Put32(&output, 0x02014b50u);
		Put16(&output, 20);
		Put16(&output, 20);
		Put16(&output, Utf8Flag);
		Put16(&output, 0);
		Put16(&output, 0);
		Put16(&output, 0);
		Put32(&output, entry.crc);
		Put32(&output, static_cast<quint32>(entry.data.size()));
		Put32(&output, static_cast<quint32>(entry.data.size()));
		Put16(&output, static_cast<quint16>(entry.name.size()));
		Put16(&output, 0);
		Put16(&output, 0);
		Put16(&output, 0);
		Put16(&output, 0);
		Put32(&output, 0);
		Put32(&output, entry.local_offset);
		output.write(entry.name);
	}
	const auto central_size = static_cast<quint32>(output.pos()) - central_offset;
	Put32(&output, 0x06054b50u);
	Put16(&output, 0);
	Put16(&output, 0);
	Put16(&output, static_cast<quint16>(entries->size()));
	Put16(&output, static_cast<quint16>(entries->size()));
	Put32(&output, central_size);
	Put32(&output, central_offset);
	Put16(&output, 0);
	if (!output.commit()) {
		if (error != nullptr) {
			*error = output.errorString();
		}
		return false;
	}
	return true;
}

QJsonObject ParseSummary(const QByteArray& data) {
	const auto document = QJsonDocument::fromJson(data);
	return document.isObject() ? document.object() : QJsonObject {};
}

} // namespace

QString LogsRoot() {
	const auto override = qEnvironmentVariable("KYTY_DIAGNOSTICS_ROOT").trimmed();
	if (!override.isEmpty()) {
		return QDir::cleanPath(QDir(override).absolutePath());
	}
	auto base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
	if (base.isEmpty()) {
		base = QCoreApplication::applicationDirPath();
	}
	return QDir(base).absoluteFilePath(QStringLiteral("KytyPS5-GT7/logs"));
}

Run::Run(QString directory, QByteArray summary)
    : m_directory(std::move(directory)), m_summary(std::move(summary)) {}

std::unique_ptr<Run> Run::Create(const RunDetails& details, QString* error,
                                 const QString& root_override) {
	const auto root = root_override.isEmpty() ? LogsRoot() : QDir(root_override).absolutePath();
	if (!QDir().mkpath(root)) {
		if (error != nullptr) {
			*error = QStringLiteral("Cannot create the diagnostic log directory: %1").arg(root);
		}
		return {};
	}
	const auto prefix =
	    QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz'Z'")) +
	    QStringLiteral("-%1-%2")
	        .arg(QCoreApplication::applicationPid())
	        .arg(CleanTitleId(details.title_id));
	QString name = prefix;
	for (int suffix = 2; QDir(root).exists(name); ++suffix) {
		name = prefix + QStringLiteral("-%1").arg(suffix);
	}
	const auto directory = QDir(root).absoluteFilePath(name);
	if (!QDir().mkpath(directory)) {
		if (error != nullptr) {
			*error = QStringLiteral("Cannot create the run log directory: %1").arg(directory);
		}
		return {};
	}
	QStringList private_paths = details.private_paths;
	private_paths.append(QFileInfo(details.emulator_path).absolutePath());
	private_paths = NormalizePrivatePaths(std::move(private_paths));
	QByteArray redaction_data;
	for (const auto& path: private_paths) {
		redaction_data += path.toUtf8().toBase64() + '\n';
	}
	QString redaction_error;
	if (!WriteAll(QDir(directory).absoluteFilePath(QString::fromLatin1(RedactionFile)),
	              redaction_data, &redaction_error)) {
		if (error != nullptr) {
			*error =
			    QStringLiteral("Cannot prepare diagnostic path redaction: %1").arg(redaction_error);
		}
		return {};
	}

	QJsonObject build;
	build.insert(QStringLiteral("git_version"), QString::fromLatin1(KYTY_GIT_VERSION));
	build.insert(QStringLiteral("git_hash"), QString::fromLatin1(KYTY_GIT_HASH));
	build.insert(QStringLiteral("git_revision"), QString::fromLatin1(KYTY_GIT_REVISION));
	build.insert(QStringLiteral("label"), QString::fromLatin1(KYTY_BUILD_LABEL));

	QJsonObject emulator;
	emulator.insert(QStringLiteral("file_name"), QFileInfo(details.emulator_path).fileName());
	const auto emulator_hash = HashFile(details.emulator_path);
	if (!emulator_hash.isEmpty()) {
		emulator.insert(QStringLiteral("sha256"), QString::fromLatin1(emulator_hash));
	}

	QJsonObject game;
	game.insert(QStringLiteral("title_id"), details.title_id);
	game.insert(QStringLiteral("version"), details.game_version);
	// Deliberately omit the installation directory and user profile.

	QJsonObject rendering;
	rendering.insert(QStringLiteral("mode"), details.mode);
	rendering.insert(QStringLiteral("output_resolution"), details.output_resolution);
	rendering.insert(QStringLiteral("desktop_internal_height"), details.desktop_height);
	rendering.insert(QStringLiteral("vr_eye_scale_percent"), details.vr_scale);

	QJsonObject summary;
	summary.insert(QStringLiteral("schema"), 1);
	summary.insert(QStringLiteral("status"), QStringLiteral("created"));
	summary.insert(QStringLiteral("started_utc"), UtcNow());
	summary.insert(QStringLiteral("last_event_utc"), UtcNow());
	summary.insert(QStringLiteral("build"), build);
	summary.insert(QStringLiteral("emulator"), emulator);
	summary.insert(QStringLiteral("game"), game);
	summary.insert(QStringLiteral("rendering"), rendering);
	summary.insert(QStringLiteral("hardware"), HardwareSummary(details.gpu_index));
	summary.insert(
	    QStringLiteral("privacy"),
	    QStringLiteral("No game files, save data, shader caches or launcher settings are "
	                   "copied. Known user, game and emulator paths are redacted from exported "
	                   "packages; review text logs before sharing."));
	QJsonArray files;
	for (const auto* name: {LauncherLogFile, PreflightOutFile, PreflightErrFile, EmulatorOutFile,
	                        EmulatorErrFile, EmulatorMainFile}) {
		files.append(QString::fromLatin1(name));
	}
	summary.insert(QStringLiteral("diagnostic_files"), files);

	auto run = std::unique_ptr<Run>(
	    new Run(directory, QJsonDocument(summary).toJson(QJsonDocument::Indented)));
	run->SaveSummary();
	run->AppendEvent(
	    QStringLiteral("run_created"),
	    QStringLiteral("Diagnostics stay local unless the user exports and shares them."));
	return run;
}

QString Run::EmulatorLogPath() const {
	return QDir(m_directory).absoluteFilePath(QString::fromLatin1(EmulatorMainFile));
}

void Run::AppendEvent(const QString& event, const QString& detail) {
	QFile file(QDir(m_directory).absoluteFilePath(QString::fromLatin1(LauncherLogFile)));
	if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		return;
	}
	QByteArray line = UtcNow().toUtf8() + " [" + event.toUtf8() + ']';
	if (!detail.isEmpty()) {
		line += ' ' + detail.toUtf8();
	}
	line += '\n';
	file.write(line);
}

void Run::SetSummaryValue(const QString& key, const QString& value) {
	auto object = ParseSummary(m_summary);
	object.insert(key, value);
	object.insert(QStringLiteral("last_event_utc"), UtcNow());
	m_summary = QJsonDocument(object).toJson(QJsonDocument::Indented);
}

void Run::SetSummaryValue(const QString& key, qint64 value) {
	auto object = ParseSummary(m_summary);
	object.insert(key, value);
	object.insert(QStringLiteral("last_event_utc"), UtcNow());
	m_summary = QJsonDocument(object).toJson(QJsonDocument::Indented);
}

void Run::SetSummaryValue(const QString& key, bool value) {
	auto object = ParseSummary(m_summary);
	object.insert(key, value);
	object.insert(QStringLiteral("last_event_utc"), UtcNow());
	m_summary = QJsonDocument(object).toJson(QJsonDocument::Indented);
}

void Run::SaveSummary() {
	QString ignored;
	WriteAll(QDir(m_directory).absoluteFilePath(QString::fromLatin1(SummaryFile)), m_summary,
	         &ignored);
}

void Run::RecordRejected(const QString& stage, const QString& message) {
	SetSummaryValue(QStringLiteral("status"), QStringLiteral("rejected"));
	SetSummaryValue(QStringLiteral("failure_stage"), stage);
	SetSummaryValue(QStringLiteral("error"), message);
	SaveSummary();
	AppendEvent(QStringLiteral("rejected"), stage + QStringLiteral(": ") + message);
}

void Run::RecordPreflight(const QByteArray& standard_output, const QByteArray& standard_error,
                          int exit_code, bool normal_exit, const QString& process_error,
                          bool interrupted) {
	QString ignored;
	WriteAll(QDir(m_directory).absoluteFilePath(QString::fromLatin1(PreflightOutFile)),
	         standard_output, &ignored);
	WriteAll(QDir(m_directory).absoluteFilePath(QString::fromLatin1(PreflightErrFile)),
	         standard_error, &ignored);
	SetSummaryValue(QStringLiteral("preflight_exit_code"), static_cast<qint64>(exit_code));
	SetSummaryValue(QStringLiteral("preflight_normal_exit"), normal_exit);
	if (!process_error.isEmpty()) {
		SetSummaryValue(QStringLiteral("preflight_process_error"), process_error);
	}
	const bool passed = !interrupted && normal_exit && exit_code == 0;
	SetSummaryValue(QStringLiteral("status"), interrupted ? QStringLiteral("preflight_cancelled")
	                                          : passed    ? QStringLiteral("preflight_passed")
	                                                      : QStringLiteral("preflight_failed"));
	SaveSummary();
	AppendEvent(interrupted ? QStringLiteral("preflight_cancelled")
	            : passed    ? QStringLiteral("preflight_passed")
	                        : QStringLiteral("preflight_failed"),
	            QStringLiteral("exit_code=%1 normal_exit=%2").arg(exit_code).arg(normal_exit));
}

void Run::ConfigureProcess(QProcess* process) const {
	process->setStandardOutputFile(
	    QDir(m_directory).absoluteFilePath(QString::fromLatin1(EmulatorOutFile)),
	    QIODevice::Truncate);
	process->setStandardErrorFile(
	    QDir(m_directory).absoluteFilePath(QString::fromLatin1(EmulatorErrFile)),
	    QIODevice::Truncate);
}

void Run::RecordStarted(qint64 process_id) {
	SetSummaryValue(QStringLiteral("status"), QStringLiteral("running"));
	SetSummaryValue(QStringLiteral("process_id"), process_id);
	SetSummaryValue(QStringLiteral("process_started_utc"), UtcNow());
	SaveSummary();
	AppendEvent(QStringLiteral("emulator_started"), QStringLiteral("pid=%1").arg(process_id));
}

void Run::RecordProcessError(int process_error, const QString& message) {
	SetSummaryValue(QStringLiteral("process_error_code"), static_cast<qint64>(process_error));
	SetSummaryValue(QStringLiteral("process_error"), message);
	SaveSummary();
	AppendEvent(QStringLiteral("process_error"),
	            QStringLiteral("code=%1 %2").arg(process_error).arg(message));
}

void Run::RecordFinished(int exit_code, bool normal_exit) {
	SetSummaryValue(QStringLiteral("exit_code"), static_cast<qint64>(exit_code));
	SetSummaryValue(QStringLiteral("normal_exit"), normal_exit);
	SetSummaryValue(QStringLiteral("finished_utc"), UtcNow());
	SetSummaryValue(QStringLiteral("status"), !normal_exit     ? QStringLiteral("crashed")
	                                          : exit_code == 0 ? QStringLiteral("finished")
	                                                           : QStringLiteral("error_exit"));
	SaveSummary();
	AppendEvent(normal_exit && exit_code == 0 ? QStringLiteral("emulator_finished")
	                                          : QStringLiteral("emulator_abnormal_exit"),
	            QStringLiteral("exit_code=%1 normal_exit=%2").arg(exit_code).arg(normal_exit));
}

QString LatestRunDirectory(const QString& root_override) {
	const auto root = root_override.isEmpty() ? LogsRoot() : QDir(root_override).absolutePath();
	QDir       dir(root);
	const auto names =
	    dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
	return names.isEmpty() ? QString() : dir.absoluteFilePath(names.constFirst());
}

bool ExportRunBundle(const QString& run_directory, const QString& zip_path, QString* error) {
	const QFileInfo run_info(run_directory);
	if (!run_info.exists() || !run_info.isDir()) {
		if (error != nullptr) {
			*error = QStringLiteral("Run log directory does not exist");
		}
		return false;
	}
	struct Candidate {
		const char* name;
		qint64      tail;
	};
	constexpr Candidate candidates[] = {
	    {SummaryFile, SmallLogTail},         {LauncherLogFile, SmallLogTail},
	    {PreflightOutFile, SmallLogTail},    {PreflightErrFile, SmallLogTail},
	    {EmulatorOutFile, ProcessLogTail},   {EmulatorErrFile, ProcessLogTail},
	    {EmulatorMainFile, EmulatorLogTail},
	};
	std::vector<ZipEntry> entries;
	QStringList           truncated_names;
	const auto            private_paths = LoadRedactionPaths(run_directory);
	for (const auto& candidate: candidates) {
		const auto path = QDir(run_directory).absoluteFilePath(QString::fromLatin1(candidate.name));
		if (!QFileInfo::exists(path)) {
			continue;
		}
		bool    truncated = false;
		QString read_error;
		auto    data = ReadTail(path, candidate.tail, &truncated, &read_error);
		if (!read_error.isEmpty()) {
			if (error != nullptr) {
				*error = QStringLiteral("Cannot read %1: %2").arg(candidate.name, read_error);
			}
			return false;
		}
		data = RedactPaths(data, private_paths);
		entries.push_back({QByteArray(candidate.name), std::move(data), 0, 0});
		if (truncated) {
			truncated_names.append(QString::fromLatin1(candidate.name));
		}
	}
	if (entries.empty()) {
		if (error != nullptr) {
			*error = QStringLiteral("The run directory has no recognized diagnostic files");
		}
		return false;
	}
	QByteArray readme =
	    "KytyPS5 GT7 diagnostic package\n"
	    "\n"
	    "This package was created locally and was not uploaded automatically. It contains only "
	    "the allow-listed run summary and text logs. It does not contain the game, save data, "
	    "shader caches or launcher settings. Known user, game and emulator paths were replaced "
	    "with <redacted-path>. Other local identifiers may remain, so review the text before "
	    "sharing.\n";
	if (!truncated_names.isEmpty()) {
		readme += "\nOnly the tail of these large logs is included: " +
		          truncated_names.join(QStringLiteral(", ")).toUtf8() + "\n";
	}
	entries.push_back({QByteArray("README.txt"), std::move(readme), 0, 0});
	return WriteStoredZip(zip_path, &entries, error);
}

} // namespace LauncherDiagnostics
