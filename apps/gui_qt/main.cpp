// SPDX-License-Identifier: MIT

#include "GuiApplicationBootstrap.hpp"
#include "GuiWindowShell.hpp"
#include "MainWorkspaceController.hpp"
#include "GuiSettings.hpp"
#include "GuiTranslations.hpp"
#include "SettingsLocationDialog.hpp"

#include <zzrk/logging/Logging.hpp>

#include <ZzPureTools/ZzApplicationWindow.h>
#include <ZzPureTools/ZzPureApplication.h>
#include <ZzWindowKit/ZzWindowKitBootstrap.h>

#include <QCoreApplication>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QStandardPaths>
#include <QDir>

#include <cstdlib>
#include <memory>

namespace {

bool hasCompletedUiState(const ZzPureTools::ZzApplicationWindow &window)
{
    const auto *connectionText =
        window.findChild<QLabel *>(QStringLiteral("connectionStatusText"));
    const auto *lastScanText =
        window.findChild<QLabel *>(QStringLiteral("lastScanText"));
    const auto *refreshButton =
        window.findChild<QToolButton *>(QStringLiteral("refreshButton"));
    return connectionText != nullptr && !connectionText->text().isEmpty()
        && connectionText->text() != zzrk::gui::guiText("正在检测设备...")
        && lastScanText != nullptr
        && lastScanText->text().startsWith(zzrk::gui::guiText("更新于 "))
        && refreshButton != nullptr && refreshButton->isEnabled()
        && window.statusBar() != nullptr
        && !window.statusBar()->currentMessage().isEmpty();
}

std::string uiStateDiagnostic(const ZzPureTools::ZzApplicationWindow &window)
{
    const auto *connectionText =
        window.findChild<QLabel *>(QStringLiteral("connectionStatusText"));
    const auto *lastScanText =
        window.findChild<QLabel *>(QStringLiteral("lastScanText"));
    const auto *refreshButton =
        window.findChild<QToolButton *>(QStringLiteral("refreshButton"));
    return QStringLiteral("connection='%1', hotplug=%2, lastScan='%3', "
                          "refreshEnabled=%4, status='%5'")
        .arg(connectionText != nullptr ? connectionText->text()
                                       : QStringLiteral("<missing>"))
        .arg(connectionText != nullptr &&
             connectionText->property("hotplugAvailable").toBool())
        .arg(lastScanText != nullptr ? lastScanText->text()
                                     : QStringLiteral("<missing>"))
        .arg(refreshButton != nullptr && refreshButton->isEnabled())
        .arg(window.statusBar() != nullptr
                 ? window.statusBar()->currentMessage()
                 : QStringLiteral("<missing>"))
        .toStdString();
}

} // namespace

int main(int argc, char *argv[])
{
    [[maybe_unused]] const auto loggingSession =
        zzrk::logging::initializeConsoleLogging("ZzRkDevTool");
    const auto bootstrap = ZzWindowKit::ZzWindowKitBootstrap::prepare();
    if (!bootstrap) {
        zzrk::gui::reportStartupFailure("WindowKit bootstrap failed",
                                        bootstrap.error());
        return EXIT_FAILURE;
    }

    ZzPureTools::ZzPureApplication application(argc, argv);
    zzrk::gui::configureApplicationMetadata(application);
    zzrk::logging::info("GUI application started");

    const bool smokeTest =
        application.arguments().contains(QStringLiteral("--smoke-test"));
    zzrk::gui::GuiApplicationOptions options;
    options.showModalDialogs = !smokeTest;
    options.smokeTest = smokeTest;
    zzrk::gui::GuiTranslations translations;
    if (!smokeTest) {
        auto applicationDirectory = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
        // macOS 的可执行文件在 .app 内；便携配置放在 .app 同级目录，避免修改签名包。
        QDir directory(applicationDirectory);
        if (directory.dirName() == QStringLiteral("MacOS")) {
            directory.cdUp(); directory.cdUp(); directory.cdUp();
            applicationDirectory = directory.absolutePath();
        }
#endif
        options.settings = std::make_shared<zzrk::gui::GuiSettingsStore>(
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation),
            applicationDirectory);
        translations.install(QStringLiteral("system"));
        const auto located = options.settings->locate();
        if (!located || !located.value()) {
            const QString reason = located ? QString{} :
                zzrk::gui::guiText("原配置不可用，请重新选择有效目录：%1")
                    .arg(located.error().technicalMessage());
            const bool quitOnClose = application.quitOnLastWindowClosed();
            application.setQuitOnLastWindowClosed(false);
            const bool selected = zzrk::gui::chooseSettingsLocation(*options.settings, nullptr, false, reason);
            application.setQuitOnLastWindowClosed(quitOnClose);
            if (!selected) return EXIT_SUCCESS;
        }
        if (!translations.install(options.settings->value(QStringLiteral("appearance/language"), QStringLiteral("system")).toString())) {
            zzrk::logging::error("GUI translation resource could not be loaded");
        }
    }
    auto buildResult = zzrk::gui::buildGuiApplication(application, options);
    if (!buildResult) {
        zzrk::gui::reportStartupFailure("GUI application build failed",
                                        buildResult.error());
        return EXIT_FAILURE;
    }
    auto *window = buildResult.value();
    auto *shell = zzrk::gui::GuiWindowShell::attachedTo(*window);
    auto *controller = shell != nullptr ? shell->controller() : nullptr;
    if (controller == nullptr) {
        zzrk::logging::error("GUI workspace controller is unavailable");
        return EXIT_FAILURE;
    }
    QObject::connect(&application, &QCoreApplication::aboutToQuit, shell,
                     [shell] { shell->savePreferences(); });

    if (smokeTest) {
        constexpr int requiredCompletedScans = 2;
        auto completedScans = std::make_shared<int>(0);

        QObject::connect(
            controller,
            &zzrk::gui::MainWorkspaceController::scanCompleted,
            &application,
            [&application, window, completedScans] {
                if (!hasCompletedUiState(*window)) {
                    zzrk::logging::error(
                        "GUI smoke test observed an incomplete UI state: " +
                        uiStateDiagnostic(*window));
                    application.exit(EXIT_FAILURE);
                    return;
                }

                ++(*completedScans);
                if (*completedScans < requiredCompletedScans) {
                    auto *refreshButton = window->findChild<QToolButton *>(
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

    window->show();
    return application.exec();
}
