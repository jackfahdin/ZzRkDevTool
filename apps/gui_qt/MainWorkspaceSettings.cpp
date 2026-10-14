// SPDX-License-Identifier: MIT
#include "GuiSettings.hpp"
#include "GuiTranslations.hpp"
#include "MainWorkspaceController.hpp"
#include "pages/FullUpgradePage.hpp"
#include "pages/ImageDownloadPage.hpp"
#include "pages/PartitionPage.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QLineEdit>
#include <QSignalBlocker>

namespace zzrk::gui {
namespace {
std::filesystem::path settingsPath(const QString &path) {
#ifdef _WIN32
  return std::filesystem::path(path.toStdWString());
#else
  const auto bytes = path.toUtf8();
  return std::filesystem::path(bytes.constData());
#endif
}
QString pathText(const std::filesystem::path &path) {
#ifdef _WIN32
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string().c_str());
#endif
}
} // namespace
void MainWorkspaceController::setSettings(
    std::shared_ptr<GuiSettingsStore> settings) {
  m_settings = std::move(settings);
  if (!m_settings)
    return;
  // 只观察用户偏好字段；设备选择、后台进度和烧录结果不进入配置。
  for (auto *page : {static_cast<QWidget *>(m_upgradePage.data()),
                     static_cast<QWidget *>(m_partitionPage.data()),
                     static_cast<QWidget *>(m_downloadPage.data())}) {
    if (page == nullptr)
      continue;
    for (auto *combo : page->findChildren<QComboBox *>())
      if (combo->objectName() == QStringLiteral("verifyModeCombo"))
        connect(combo, &QComboBox::currentIndexChanged, this,
                &MainWorkspaceController::preferencesChanged);
    for (auto *check : page->findChildren<QCheckBox *>())
      connect(check, &QCheckBox::toggled, this,
              &MainWorkspaceController::preferencesChanged);
  }
  if (m_downloadPage) {
    connect(m_downloadPage, &ImageDownloadPage::layoutChanged, this,
            &MainWorkspaceController::preferencesChanged);
    connect(m_downloadPage, &ImageDownloadPage::planChanged, this,
            &MainWorkspaceController::preferencesChanged);
  }
}
void MainWorkspaceController::captureSettings() {
  if (!m_settings)
    return;
  m_settings->setValue(QStringLiteral("upgrade/image"), m_firmwarePath);
  if (m_upgradePage) {
    auto *verify = m_upgradePage->findChild<QComboBox *>(
        QStringLiteral("verifyModeCombo"));
    const QStringList modes{QStringLiteral("critical"), QStringLiteral("all"),
                            QStringLiteral("none")};
    m_settings->setValue(
        QStringLiteral("upgrade/verify"),
        modes.value(verify ? verify->currentIndex() : 0, modes.first()));
    m_settings->setValue(QStringLiteral("upgrade/reboot"),
                         m_upgradePage->rebootAfterFlash());
  }
  if (m_partitionPage)
    m_settings->setValue(QStringLiteral("partition/verify"),
                         m_partitionPage->verifyWrite());
  if (m_downloadPage) {
    m_settings->setValue(QStringLiteral("download/layout"),
                         m_downloadPage->layoutCombo()->currentIndex() == 1
                             ? QStringLiteral("parameter")
                             : QStringLiteral("device"));
    m_settings->setValue(QStringLiteral("download/reboot"),
                         m_downloadPage->rebootCheckBox()->isChecked());
  }
  m_settings->setValue(QStringLiteral("download/parameter"),
                       pathText(m_downloadParameterPath));
  m_settings->setValue(QStringLiteral("download/directory"),
                       pathText(m_downloadDirectoryPath));
  QVariantMap images, selections, verifications;
  for (const auto &[target, path] : m_downloadImages)
    images.insert(QString::fromStdString(target), pathText(path));
  for (const auto &[target, selected] : m_downloadSelections)
    selections.insert(QString::fromStdString(target), selected);
  for (const auto &[target, verify] : m_downloadVerifications)
    verifications.insert(QString::fromStdString(target), verify);
  m_settings->setValue(QStringLiteral("download/images"), images);
  m_settings->setValue(QStringLiteral("download/selections"), selections);
  m_settings->setValue(QStringLiteral("download/verifications"), verifications);
}
void MainWorkspaceController::restoreSettings() {
  if (!m_settings || usbOperationRunning() || m_prepareWatcher.isRunning())
    return;
  if (m_upgradePage) {
    auto *verify = m_upgradePage->findChild<QComboBox *>(
        QStringLiteral("verifyModeCombo"));
    auto *reboot = m_upgradePage->findChild<QCheckBox *>(
        QStringLiteral("rebootAfterFlashCheckBox"));
    if (verify) {
      const QSignalBlocker blocker(verify);
      const auto mode = m_settings
                            ->value(QStringLiteral("upgrade/verify"),
                                    QStringLiteral("critical"))
                            .toString();
      verify->setCurrentIndex(mode == QStringLiteral("all")    ? 1
                              : mode == QStringLiteral("none") ? 2
                                                               : 0);
    }
    if (reboot) {
      const QSignalBlocker blocker(reboot);
      reboot->setChecked(
          m_settings->value(QStringLiteral("upgrade/reboot"), true).toBool());
    }
    m_preparedUpgrade.reset();
    m_progressTracker.reset();
    m_startUpgradeAfterPreparation = false;
    m_firmwarePath =
        m_settings->value(QStringLiteral("upgrade/image")).toString();
    m_upgradePage->setFirmwarePath(m_firmwarePath);
    m_upgradePage->setPlanPreview({});
    // 启动仅恢复路径，不沿用 MD5 成功或执行 USB 访问；开始升级时重新预检。
    m_upgradePage->setStatus(
        m_firmwarePath.isEmpty() ? guiText("准备就绪")
        : QFileInfo(m_firmwarePath).isFile()
            ? guiText("已恢复固件路径，升级前将重新校验。")
            : guiText("上次选择的固件不存在，请重新选择。"));
  }
  if (m_partitionPage) {
    auto *verify = m_partitionPage->findChild<QCheckBox *>(
        QStringLiteral("verifyPartitionWriteCheckBox"));
    if (verify) {
      const QSignalBlocker blocker(verify);
      verify->setChecked(
          m_settings->value(QStringLiteral("partition/verify"), true).toBool());
    }
  }
  if (m_downloadPage) {
    const QSignalBlocker pageBlock(m_downloadPage),
        layoutBlock(m_downloadPage->layoutCombo()),
        rebootBlock(m_downloadPage->rebootCheckBox());
    m_downloadLayout.reset();
    m_downloadImages.clear();
    m_downloadSelections.clear();
    m_downloadVerifications.clear();
    m_validDownloadImages.clear();
    m_downloadParameterPath = settingsPath(
        m_settings->value(QStringLiteral("download/parameter")).toString());
    m_downloadDirectoryPath = settingsPath(
        m_settings->value(QStringLiteral("download/directory")).toString());
    const bool parameter =
        m_settings
            ->value(QStringLiteral("download/layout"), QStringLiteral("device"))
            .toString() == QStringLiteral("parameter");
    m_downloadPage->layoutCombo()->setCurrentIndex(parameter ? 1 : 0);
    m_downloadPage->rebootCheckBox()->setChecked(
        m_settings->value(QStringLiteral("download/reboot"), true).toBool());
    setGuiText(m_downloadPage->directoryEdit(), "text",
               pathText(m_downloadDirectoryPath));
    setGuiText(m_downloadPage->layoutPathEdit(), "text",
               parameter ? pathText(m_downloadParameterPath) : QString{});
    if (parameter && QFileInfo(pathText(m_downloadParameterPath)).isFile()) {
      auto parsed = image::parseRockchipParameterFile(m_downloadParameterPath);
      if (auto *layout = std::get_if<image::RockchipParameterLayout>(&parsed))
        m_downloadLayout = std::move(*layout);
    }
    const auto images =
        m_settings->value(QStringLiteral("download/images")).toMap();
    for (auto it = images.cbegin(); it != images.cend(); ++it)
      assignDownloadImage(it.key().toStdString(),
                          settingsPath(it.value().toString()), false, false);
    m_downloadDraftRestored = !images.isEmpty();
    const auto selections =
        m_settings->value(QStringLiteral("download/selections")).toMap();
    for (auto it = selections.cbegin(); it != selections.cend(); ++it)
      m_downloadSelections.insert_or_assign(it.key().toStdString(),
                                            it.value().toBool());
    const auto verifications =
        m_settings->value(QStringLiteral("download/verifications")).toMap();
    for (auto it = verifications.cbegin(); it != verifications.cend(); ++it)
      m_downloadVerifications.insert_or_assign(it.key().toStdString(),
                                               it.value().toBool());
    // 文件映射保留为草稿，重新检查有效性，设备 GPT 必须重新从当前设备读取。
    rebuildDownloadTable();
    m_downloadPage->setStatus(
        parameter && !m_downloadLayout
            ? guiText("上次选择的 parameter 文件不存在或无效，请重新选择。")
            : guiText("已恢复镜像配置草稿。"));
  }
  updateUpgradeControls();
  updateDownloadControls();
}
void MainWorkspaceController::revalidateDownloadDraft() {
  const auto images = m_downloadImages;
  const auto selections = m_downloadSelections;
  const auto verifications = m_downloadVerifications;
  m_validDownloadImages.clear();
  for (const auto &[target, path] : images)
    assignDownloadImage(target, path, false, false);
  m_downloadSelections = selections;
  m_downloadVerifications = verifications;
  rebuildDownloadTable();
}
} // namespace zzrk::gui
