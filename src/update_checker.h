#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QWidget;

class UpdateChecker : public QObject {
    Q_OBJECT

public:
    struct ReleaseInfo {
        QString tagName;
        QString name;
        QString body;
        QString publishedAt;
        QUrl htmlUrl;
        QUrl assetUrl;
        QString assetName;
        bool isPrerelease = false;
    };

    explicit UpdateChecker(QWidget* parentWindow, QObject* parent = nullptr);

    void checkForUpdates(bool manual);

    static bool isNewerVersion(const QString& remoteTag, const QString& currentVersion,
                               const QString& currentDate = QString(),
                               const QString& currentHash = QString());

signals:
    void checkFinished(bool manual, bool updateAvailable);

private:
    void fetchReleases(const QUrl& url, bool isFallback, bool manual);
    void handleReleaseReply(const QByteArray& payload, int httpStatus, bool isFallback, bool manual);
    void showUpdateDialog(const ReleaseInfo& release);
    void downloadAsset(const ReleaseInfo& release);

    QWidget* parentWindow = nullptr;
    QNetworkAccessManager* networkManager = nullptr;
    bool checkInProgress = false;
};
