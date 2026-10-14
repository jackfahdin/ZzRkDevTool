// SPDX-License-Identifier: MIT
#pragma once
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <ZzCore/ZzResult.h>

namespace zzrk::gui {
enum class GuiSettingsLocation { User, Application, Custom };
class GuiSettingsStore {
public:
  // 调用方提供平台目录，测试可替换为临时目录；存储只在 GUI 线程访问。
  GuiSettingsStore(QString userConfigDir, QString applicationDir);
  // true 表示已加载配置，false 表示首次启动；错误由首次选择弹窗处理。
  ZzCore::ZzResult<bool> locate();
  ZzCore::ZzResult<void> initialize(GuiSettingsLocation location,
                                    QString customDir = {});
  ZzCore::ZzResult<void> relocate(GuiSettingsLocation location,
                                  QString customDir = {});
  // 页面操作先更新内存，再统一同步，避免每个控件维护一个 QSettings。
  QVariant value(QString key, QVariant fallback = {}) const;
  void setValue(QString key, QVariant value);
  ZzCore::ZzResult<void> sync();
  // 仅重置偏好与草稿，不改变当前配置位置。
  ZzCore::ZzResult<void> reset();
  QString directory() const { return directory_; }
  QString filePath() const;
  GuiSettingsLocation location() const { return location_; }
  QString userDirectory() const { return userDirectory_; }
  QString applicationDirectory() const { return applicationDirectory_; }

private:
  ZzCore::ZzResult<void> select(GuiSettingsLocation location,
                                const QString &customDir, bool migrate);
  ZzCore::ZzResult<void> writeLocator(GuiSettingsLocation location,
                                      const QString &directory);
  QString userDirectory_, applicationDirectory_, directory_;
  GuiSettingsLocation location_ = GuiSettingsLocation::User;
  QVariantMap data_;
  bool locatorUncertain_ = false;
};
} // namespace zzrk::gui
