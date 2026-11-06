#include "compatibilityDatabase.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace {

constexpr char FILE_NAME[]    = "compatibility_db.json";
constexpr char URL[]          = "https://kytyps5.github.io/data/compatibility.json";
constexpr int  RETRY_COUNT    = 3;
constexpr int  RETRY_DELAY_MS = 750;

struct LoadResult {
	QMap<QString, CompatibilityEntry> entries;
	QString                           error;
};

QString TitleKey(const QString& title_id) {
	return title_id.trimmed().toUpper();
}

Configuration::GameStatus StatusFromText(const QString& text) {
	const auto value = text.trimmed();
	if (value == QStringLiteral("InGame") || value == QStringLiteral("In game")) {
		return Configuration::GameStatus::InGame;
	}
	if (value == QStringLiteral("MainMenu") || value == QStringLiteral("Main menu")) {
		return Configuration::GameStatus::MainMenu;
	}
	if (value == QStringLiteral("Logo")) {
		return Configuration::GameStatus::Logo;
	}
	if (value == QStringLiteral("DoesntBoot") || value == QStringLiteral("Doesn't boot")) {
		return Configuration::GameStatus::DoesntBoot;
	}
	return Configuration::GameStatus::Unknown;
}

QString StatusToText(Configuration::GameStatus status) {
	switch (status) {
		case Configuration::GameStatus::InGame: return QStringLiteral("InGame");
		case Configuration::GameStatus::MainMenu: return QStringLiteral("MainMenu");
		case Configuration::GameStatus::Logo: return QStringLiteral("Logo");
		case Configuration::GameStatus::DoesntBoot: return QStringLiteral("DoesntBoot");
		case Configuration::GameStatus::Unknown: return QStringLiteral("Unknown");
	}
	return QStringLiteral("Unknown");
}

LoadResult Parse(const QByteArray& data) {
	LoadResult      ret;
	QJsonParseError parse_error;
	const auto      doc = QJsonDocument::fromJson(data, &parse_error);
	if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
		ret.error = QStringLiteral("Invalid compatibility JSON: %1").arg(parse_error.errorString());
		return ret;
	}

	const auto root = doc.object();
	for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
		const auto title_id = TitleKey(it.key());
		const auto object   = it.value().toObject();
		if (title_id.isEmpty() || object.isEmpty()) {
			continue;
		}
		ret.entries.insert(title_id,
		                   {StatusFromText(object.value(QStringLiteral("status")).toString()),
		                    object.value(QStringLiteral("comment")).toString()});
	}
	return ret;
}

} // namespace

CompatibilityDatabase::CompatibilityDatabase(bool local, QObject* parent)
    : QObject(parent), m_network(this), m_local(local) {}

const CompatibilityEntry* CompatibilityDatabase::Find(const QString& title_id) const {
	const auto entry = m_entries.constFind(TitleKey(title_id));
	return entry != m_entries.constEnd() ? &entry.value() : nullptr;
}

void CompatibilityDatabase::Load() {
	if (m_local) {
		QFile file(QDir(".").absoluteFilePath(FILE_NAME));
		if (!file.exists()) {
			emit Updated();
			return;
		}
		if (!file.open(QIODevice::ReadOnly)) {
			qWarning() << "Could not open compatibility database:" << file.errorString();
			return;
		}
		auto result = Parse(file.readAll());
		if (!result.error.isEmpty()) {
			qWarning() << result.error;
			return;
		}
		m_entries = std::move(result.entries);
		emit Updated();
		return;
	}

	if (!m_loading) {
		m_loading = true;
		Fetch(0);
	}
}

void CompatibilityDatabase::Fetch(int attempt) {
	// Keep requests on the UI event loop. Destroying the launcher cancels its replies,
	// rather than waiting for a global worker's nested network loop and retry sleeps.
	QNetworkRequest request(QUrl(QString::fromLatin1(URL)));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
	                     QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("User-Agent", "Tohi-GT7-Launcher/1.0.2");
	request.setTransferTimeout(15000);
	auto* reply = m_network.get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply, attempt] {
		auto result = reply->error() == QNetworkReply::NoError
		                  ? Parse(reply->readAll())
		                  : LoadResult {{}, reply->errorString()};
		reply->deleteLater();
		if (!result.error.isEmpty()) {
			if (attempt < RETRY_COUNT) {
				QTimer::singleShot(RETRY_DELAY_MS * (attempt + 1), this,
				                   [this, attempt] { Fetch(attempt + 1); });
				return;
			}
			m_loading = false;
			qWarning() << "Could not refresh compatibility database:" << result.error;
			return;
		}
		m_loading = false;
		m_entries = std::move(result.entries);
		emit Updated();
	});
}

void CompatibilityDatabase::SetStatus(const QString& title_id, Configuration::GameStatus status) {
	const auto key = TitleKey(title_id);
	if (!m_local || key.isEmpty()) {
		return;
	}
	m_entries[key].status = status;
	Save();
}

void CompatibilityDatabase::SetComment(const QString& title_id, const QString& comment) {
	const auto key = TitleKey(title_id);
	if (!m_local || key.isEmpty()) {
		return;
	}
	m_entries[key].comment = comment;
	Save();
}

void CompatibilityDatabase::Save() const {
	QJsonObject root;
	for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
		root.insert(it.key(),
		            QJsonObject {{QStringLiteral("status"), StatusToText(it.value().status)},
		                         {QStringLiteral("comment"), it.value().comment}});
	}

	QSaveFile  file(QDir(".").absoluteFilePath(FILE_NAME));
	const auto data = QJsonDocument(root).toJson(QJsonDocument::Indented);
	if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
		qWarning() << "Could not save compatibility database:" << file.errorString();
	}
}
