#include "update_checker.h"

#include <QCoreApplication>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>
#include <QWidget>
#include <memory>

namespace {

constexpr const char* kGitHubRepo = "kazwtto/PKGInstall";

struct ParsedSemVer {
    bool valid = false;
    QVersionNumber core;
    QString prerelease;
};

ParsedSemVer parseSemVer(const QString& input) {
    QString s = input.trimmed();
    if (s.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        s.remove(0, 1);
    }

    static const QRegularExpression re(QStringLiteral(R"(^(\d+(?:\.\d+){1,3})(?:-([0-9A-Za-z.-]+))?$)"));
    const QRegularExpressionMatch match = re.match(s);
    if (!match.hasMatch()) {
        return {};
    }

    ParsedSemVer result;
    result.valid = true;
    result.core = QVersionNumber::fromString(match.captured(1)).normalized();
    result.prerelease = match.captured(2);
    return result;
}

struct ParsedCiTag {
    bool valid = false;
    QDate date;
    QString hash;
};

ParsedCiTag parseCiTag(const QString& tag) {
    static const QRegularExpression re(
        QStringLiteral(R"(^PKGInstall-(\d{4}-\d{2}-\d{2})-([0-9A-Za-z]+)$)"));
    const QRegularExpressionMatch match = re.match(tag.trimmed());
    if (!match.hasMatch()) {
        return {};
    }

    const QDate date = QDate::fromString(match.captured(1), Qt::ISODate);
    if (!date.isValid()) {
        return {};
    }

    return {true, date, match.captured(2)};
}

QNetworkRequest makeGitHubRequest(const QUrl& url) {
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(15000);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("PKGInstall/%1").arg(QCoreApplication::applicationVersion()));
    req.setRawHeader("Accept", "application/vnd.github+json");
    return req;
}

int scoreAssetForPlatform(const QString& name) {
    const QString lower = name.toLower();
#ifdef _WIN32
    if (lower.contains(QStringLiteral("win64")) || lower.contains(QStringLiteral("windows"))) {
        if (lower.endsWith(QStringLiteral(".exe"))) {
            return 100;
        }
        if (lower.endsWith(QStringLiteral(".zip"))) {
            return 90;
        }
        return 70;
    }
    if (lower.endsWith(QStringLiteral(".exe")) || lower.endsWith(QStringLiteral(".msi"))) {
        return 50;
    }
    return 0;
#elif defined(__APPLE__)
    if (lower.contains(QStringLiteral("mac")) || lower.contains(QStringLiteral("osx")) ||
        lower.contains(QStringLiteral("darwin"))) {
        if (lower.endsWith(QStringLiteral(".dmg"))) {
            return 100;
        }
        if (lower.endsWith(QStringLiteral(".tar.gz")) || lower.endsWith(QStringLiteral(".zip"))) {
            return 90;
        }
        return 70;
    }
    return 0;
#else
    const bool inAppImage = qEnvironmentVariableIsSet("APPIMAGE");
    if (inAppImage && lower.contains(QStringLiteral("appimage"))) {
        return 100;
    }
    if (lower.contains(QStringLiteral("appimage"))) {
        return 85;
    }
    if (lower.contains(QStringLiteral("ubuntu")) || lower.contains(QStringLiteral("linux"))) {
        return 80;
    }
    return 0;
#endif
}

UpdateChecker::ReleaseInfo parseReleaseObject(const QJsonObject& obj) {
    UpdateChecker::ReleaseInfo info;
    info.tagName = obj.value(QStringLiteral("tag_name")).toString();
    info.name = obj.value(QStringLiteral("name")).toString();
    info.body = obj.value(QStringLiteral("body")).toString();
    info.publishedAt = obj.value(QStringLiteral("published_at")).toString();
    info.isPrerelease = obj.value(QStringLiteral("prerelease")).toBool(false);

    const QUrl htmlUrl(obj.value(QStringLiteral("html_url")).toString());
    if (htmlUrl.isValid() && htmlUrl.scheme() == QLatin1String("https")) {
        info.htmlUrl = htmlUrl;
    }

    int bestScore = 0;
    const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue& val : assets) {
        if (!val.isObject()) {
            continue;
        }
        const QJsonObject asset = val.toObject();
        const QString assetName = asset.value(QStringLiteral("name")).toString();
        const QUrl downloadUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        if (!downloadUrl.isValid() || downloadUrl.scheme() != QLatin1String("https")) {
            continue;
        }

        const int score = scoreAssetForPlatform(assetName);
        if (score > bestScore) {
            bestScore = score;
            info.assetUrl = downloadUrl;
            info.assetName = QFileInfo(assetName).fileName();
        }
    }

    return info;
}

QString compiledBuildDate() {
#ifdef PKGINSTALL_BUILD_DATE
    return QStringLiteral(PKGINSTALL_BUILD_DATE);
#else
    return QString();
#endif
}

QString compiledGitHash() {
#ifdef PKGINSTALL_GIT_HASH
    return QStringLiteral(PKGINSTALL_GIT_HASH);
#else
    return QString();
#endif
}

} // namespace

UpdateChecker::UpdateChecker(QWidget* parentWindow, QObject* parent)
    : QObject(parent), parentWindow(parentWindow), networkManager(new QNetworkAccessManager(this)) {
}

bool UpdateChecker::isNewerVersion(const QString& remoteTag, const QString& currentVersion,
                                   const QString& currentDate, const QString& currentHash) {
    const ParsedSemVer remoteSemVer = parseSemVer(remoteTag);
    const ParsedSemVer currentSemVer = parseSemVer(currentVersion);
    if (remoteSemVer.valid && currentSemVer.valid) {
        const int cmp = QVersionNumber::compare(remoteSemVer.core, currentSemVer.core);
        if (cmp != 0) {
            return cmp > 0;
        }
        if (remoteSemVer.prerelease.isEmpty() != currentSemVer.prerelease.isEmpty()) {
            return remoteSemVer.prerelease.isEmpty();
        }
        return QString::compare(remoteSemVer.prerelease, currentSemVer.prerelease,
                                Qt::CaseInsensitive) > 0;
    }

    const ParsedCiTag remoteCi = parseCiTag(remoteTag);
    if (remoteCi.valid) {
        if (!currentHash.isEmpty() &&
            remoteCi.hash.compare(currentHash, Qt::CaseInsensitive) == 0) {
            return false;
        }
        const QDate buildDate = QDate::fromString(currentDate, Qt::ISODate);
        if (buildDate.isValid()) {
            return remoteCi.date > buildDate;
        }
    }

    return false;
}

void UpdateChecker::checkForUpdates(bool manual) {
    if (checkInProgress) {
        return;
    }
    checkInProgress = true;

    const QUrl latestUrl(
        QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(QLatin1String(kGitHubRepo)));
    fetchReleases(latestUrl, false, manual);
}

void UpdateChecker::fetchReleases(const QUrl& url, bool isFallback, bool manual) {
    if (!url.isValid() || url.scheme() != QLatin1String("https")) {
        checkInProgress = false;
        emit checkFinished(manual, false);
        return;
    }

    QNetworkReply* reply = networkManager->get(makeGitHubRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, isFallback, manual]() {
        const QVariant statusVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int httpStatus = statusVar.isValid() ? statusVar.toInt() : 0;
        const QNetworkReply::NetworkError netErr = reply->error();
        const QString errStr = reply->errorString();
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (!isFallback && httpStatus == 404) {
            const QUrl fallbackUrl(
                QStringLiteral("https://api.github.com/repos/%1/releases?per_page=10")
                    .arg(QLatin1String(kGitHubRepo)));
            fetchReleases(fallbackUrl, true, manual);
            return;
        }

        if (netErr != QNetworkReply::NoError && httpStatus != 404) {
            checkInProgress = false;
            if (manual) {
                QMessageBox::warning(
                    parentWindow, tr("Update Check"),
                    tr("Could not check for updates:\n%1").arg(errStr));
            }
            emit checkFinished(manual, false);
            return;
        }

        handleReleaseReply(payload, httpStatus, isFallback, manual);
    });
}

void UpdateChecker::handleReleaseReply(const QByteArray& payload, int httpStatus, bool isFallback,
                                         bool manual) {
    checkInProgress = false;

    if (httpStatus == 404) {
        if (manual) {
            QMessageBox::information(
                parentWindow, tr("Update Check"),
                tr("No published releases were found for this repository."));
        }
        emit checkFinished(manual, false);
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (manual) {
            QMessageBox::warning(parentWindow, tr("Update Check"),
                                 tr("Received an invalid response from GitHub."));
        }
        emit checkFinished(manual, false);
        return;
    }

    ReleaseInfo candidate;
    bool hasCandidate = false;

    if (!isFallback && doc.isObject()) {
        candidate = parseReleaseObject(doc.object());
        hasCandidate = !candidate.tagName.isEmpty();
    } else if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        for (const QJsonValue& val : arr) {
            if (!val.isObject()) {
                continue;
            }
            const QJsonObject obj = val.toObject();
            if (obj.value(QStringLiteral("draft")).toBool(false)) {
                continue;
            }
            candidate = parseReleaseObject(obj);
            if (!candidate.tagName.isEmpty()) {
                hasCandidate = true;
                break;
            }
        }
    }

    const QString currentVersion = QCoreApplication::applicationVersion();
    if (!hasCandidate || !isNewerVersion(candidate.tagName, currentVersion,
                                         compiledBuildDate(), compiledGitHash())) {
        if (manual) {
            QMessageBox::information(
                parentWindow, tr("Update Check"),
                tr("You are using the latest version (%1).").arg(currentVersion));
        }
        emit checkFinished(manual, false);
        return;
    }

    emit checkFinished(manual, true);
    showUpdateDialog(candidate);
}

void UpdateChecker::showUpdateDialog(const ReleaseInfo& release) {
    QMessageBox msgBox(parentWindow);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setWindowTitle(tr("Update Available"));
    msgBox.setTextFormat(Qt::PlainText);

    const QString displayName = release.name.isEmpty() ? release.tagName : release.name;
    msgBox.setText(tr("A new version of PKGInstall is available: %1\nCurrent version: %2")
                       .arg(displayName, QCoreApplication::applicationVersion()));

    if (!release.body.trimmed().isEmpty()) {
        QString notes = release.body.trimmed();
        constexpr qsizetype kMaxNotesLength = 1200;
        if (notes.size() > kMaxNotesLength) {
            notes = notes.left(kMaxNotesLength) + QStringLiteral("\n...");
        }
        msgBox.setInformativeText(notes);
    }

    QPushButton* downloadButton = nullptr;
    if (release.assetUrl.isValid()) {
        downloadButton = msgBox.addButton(tr("Download"), QMessageBox::AcceptRole);
    }
    QPushButton* openPageButton = nullptr;
    if (release.htmlUrl.isValid()) {
        openPageButton = msgBox.addButton(tr("Open on GitHub"), QMessageBox::ActionRole);
    }
    msgBox.addButton(tr("Later"), QMessageBox::RejectRole);

    msgBox.exec();

    if (downloadButton && msgBox.clickedButton() == downloadButton) {
        downloadAsset(release);
    } else if (openPageButton && msgBox.clickedButton() == openPageButton) {
        QDesktopServices::openUrl(release.htmlUrl);
    }
}

void UpdateChecker::downloadAsset(const ReleaseInfo& release) {
    if (!release.assetUrl.isValid() || release.assetUrl.scheme() != QLatin1String("https")) {
        return;
    }

    QString downloadsDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloadsDir.isEmpty()) {
        downloadsDir = QDir::homePath();
    }
    QDir().mkpath(downloadsDir);

    const QString safeName =
        release.assetName.isEmpty() ? QStringLiteral("PKGInstall-update.zip") : release.assetName;
    const QString targetPath = QDir(downloadsDir).filePath(safeName);

    auto* outFile = new QFile(targetPath);
    if (!outFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete outFile;
        QMessageBox::critical(parentWindow, tr("Download Failed"),
                              tr("Could not create file:\n%1").arg(targetPath));
        return;
    }

    QPointer<QProgressDialog> progress =
        new QProgressDialog(tr("Downloading %1...").arg(safeName), tr("Cancel"), 0, 100, parentWindow);
    progress->setWindowTitle(tr("Downloading Update"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setMinimumDuration(0);
    progress->setValue(0);

    QNetworkRequest req(release.assetUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("PKGInstall/%1").arg(QCoreApplication::applicationVersion()));

    QNetworkReply* reply = networkManager->get(req);
    outFile->setParent(reply);

    auto writeError = std::make_shared<bool>(false);

    connect(reply, &QNetworkReply::readyRead, reply, [reply, outFile, writeError]() {
        if (!*writeError && outFile->isOpen()) {
            const QByteArray chunk = reply->readAll();
            if (outFile->write(chunk) != chunk.size()) {
                *writeError = true;
                reply->abort();
            }
        }
    });

    connect(reply, &QNetworkReply::downloadProgress, progress,
            [progress](qint64 received, qint64 total) {
                if (!progress) {
                    return;
                }
                if (total > 0) {
                    progress->setRange(0, 100);
                    progress->setValue(static_cast<int>((received * 100) / total));
                } else {
                    progress->setRange(0, 0);
                }
            });

    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, outFile, progress, writeError, targetPath, downloadsDir]() {
                const bool aborted = (reply->error() == QNetworkReply::OperationCanceledError);
                const bool failed = *writeError || (reply->error() != QNetworkReply::NoError);
                const QString errStr =
                    *writeError ? tr("Failed to write downloaded data to disk.") : reply->errorString();

                if (!failed && outFile->isOpen()) {
                    const QByteArray rest = reply->readAll();
                    if (!rest.isEmpty() && outFile->write(rest) != rest.size()) {
                        *writeError = true;
                    }
                    outFile->flush();
                }
                outFile->close();

                if (progress) {
                    progress->close();
                    progress->deleteLater();
                }
                reply->deleteLater();

                if (failed || *writeError) {
                    outFile->remove();
                    if (!aborted || *writeError) {
                        QMessageBox::critical(parentWindow, tr("Download Failed"),
                                              tr("Failed to download update:\n%1").arg(errStr));
                    }
                    return;
                }

                QDesktopServices::openUrl(QUrl::fromLocalFile(downloadsDir));
                QMessageBox::information(
                    parentWindow, tr("Download Complete"),
                    tr("Update downloaded to:\n%1").arg(targetPath));
            });
}
