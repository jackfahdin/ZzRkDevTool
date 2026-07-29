// SPDX-License-Identifier: MIT

#include "MainWindow.hpp"

#include <zzrk/logging/Logging.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>

#include <cstdlib>
#include <memory>

namespace {

bool hasCompletedUiState(const MainWindow &window)
{
    const auto *connectionText =
        window.findChild<QLabel *>(QStringLiteral("connectionStatusText"));
    const auto *lastScanText =
        window.findChild<QLabel *>(QStringLiteral("lastScanText"));
    const auto *refreshButton =
        window.findChild<QToolButton *>(QStringLiteral("refreshButton"));
    return connectionText != nullptr && !connectionText->text().isEmpty()
        && connectionText->text() != QStringLiteral("正在检测设备...")
        && lastScanText != nullptr
        && lastScanText->text().startsWith(QStringLiteral("更新于 "))
        && refreshButton != nullptr && refreshButton->isEnabled()
        && window.statusBar() != nullptr
        && !window.statusBar()->currentMessage().isEmpty();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ZzRkDevTool"));
    QCoreApplication::setApplicationName(QStringLiteral("ZzRkDevTool"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ZZRK_VERSION));
    [[maybe_unused]] const auto loggingSession =
        zzrk::logging::initializeConsoleLogging("ZzRkDevTool");
    zzrk::logging::info("GUI application started");

    const bool smokeTest =
        application.arguments().contains(QStringLiteral("--smoke-test"));
    MainWindow window;

    if (smokeTest) {
        constexpr int requiredCompletedScans = 2;
        auto completedScans = std::make_shared<int>(0);

        QObject::connect(
            &window,
            &MainWindow::scanCompleted,
            &application,
            [&application, &window, completedScans] {
                if (!hasCompletedUiState(window)) {
                    zzrk::logging::error(
                        "GUI smoke test observed an incomplete UI state");
                    application.exit(EXIT_FAILURE);
                    return;
                }

                ++(*completedScans);
                if (*completedScans < requiredCompletedScans) {
                    auto *refreshButton = window.findChild<QToolButton *>(
                        QStringLiteral("refreshButton"));
                    refreshButton->click();
                    if (refreshButton->isEnabled()) {
                        zzrk::logging::error(
                            "GUI smoke test could not start a manual scan");
                        application.exit(EXIT_FAILURE);
                    }
                    return;
                }

                application.exit(EXIT_SUCCESS);
            });

        QTimer::singleShot(12000, &application, [&application] {
            zzrk::logging::error(
                "GUI smoke test timed out waiting for asynchronous scans");
            application.exit(EXIT_FAILURE);
        });
    }

    window.show();
    return application.exec();
}
