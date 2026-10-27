#ifndef LAUNCHER_DIAGNOSTICS_H
#define LAUNCHER_DIAGNOSTICS_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>

class QProcess;

namespace LauncherDiagnostics {

struct RunDetails {
	QString title_id;
	QString game_version;
	QString output_resolution;
	QString mode;
	int     desktop_height = 0;
	int     vr_scale       = 100;
	int     gpu_index      = -1;
	QString emulator_path;
	// These paths are kept only in a private sidecar inside the local run directory. They are
	// used to remove known user, game and emulator paths when a support ZIP is exported.
	QStringList private_paths;
};

// One private, local diagnostic directory is created for every GT7 launch attempt. It contains
// logs and a small machine-readable summary, but never copies the game, save data, caches or the
// launcher's settings file.
class Run {
public:
	static std::unique_ptr<Run> Create(const RunDetails& details, QString* error,
	                                   const QString& root_override = {});
	~Run() = default;

	Run(const Run&)            = delete;
	Run& operator=(const Run&) = delete;

	[[nodiscard]] const QString& Directory() const { return m_directory; }
	[[nodiscard]] QString        EmulatorLogPath() const;

	void RecordRejected(const QString& stage, const QString& message);
	void RecordPreflight(const QByteArray& standard_output, const QByteArray& standard_error,
	                     int exit_code, bool normal_exit, const QString& process_error,
	                     bool interrupted);
	void ConfigureProcess(QProcess* process) const;
	void RecordStarted(qint64 process_id);
	void RecordProcessError(int process_error, const QString& message);
	void RecordFinished(int exit_code, bool normal_exit);

private:
	Run(QString directory, QByteArray summary);

	void AppendEvent(const QString& event, const QString& detail = {});
	void SetSummaryValue(const QString& key, const QString& value);
	void SetSummaryValue(const QString& key, qint64 value);
	void SetSummaryValue(const QString& key, bool value);
	void SaveSummary();

	QString    m_directory;
	QByteArray m_summary;
};

[[nodiscard]] QString LogsRoot();
[[nodiscard]] QString LatestRunDirectory(const QString& root_override = {});

// Creates a local ZIP with an explicit allow-list of diagnostic files. Large text logs are
// represented by their tail so the support package remains practical. Nothing is uploaded.
bool ExportRunBundle(const QString& run_directory, const QString& zip_path, QString* error);

} // namespace LauncherDiagnostics

#endif // LAUNCHER_DIAGNOSTICS_H
