// SPDX-License-Identifier: MIT
#include "GuiSettings.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <ZzCore/ZzQtSettingsStore.h>

namespace zzrk::gui {
namespace {
using ZzCore::ZzResult;
ZzCore::ZzError error(const QString &message, const QString &path = {}) {
  return {ZzCore::ZzErrorCode::Io, message, path};
}
QVariant normalized(const QString &key, const QVariant &value) {
  QStringList choices;
  if (key == "appearance/theme")
    choices = {"system", "light", "dark"};
  else if (key == "appearance/language")
    choices = {"system", "zh_CN", "en"};
  else if (key == "upgrade/verify")
    choices = {"critical", "all", "none"};
  else if (key == "download/layout")
    choices = {"device", "parameter"};
  return choices.isEmpty() || choices.contains(value.toString())
             ? value
             : QVariant(choices.first());
}
QVariantMap defaults() {
  return {{"appearance/theme", "system"},
          {"appearance/language", "system"},
          {"appearance/fontFamily", ""},
          {"appearance/fontPointSize", 0},
          {"appearance/accentColor", ""},
          {"upgrade/verify", "critical"},
          {"download/layout", "device"}};
}
QString cleanDirectory(const QString &path) {
  QFileInfo info(path);
  const QString absolute = QDir::cleanPath(info.absoluteFilePath());
  QStringList missing;
  // 未创建的目录也解析已有祖先的符号链接，避免创建后路径身份改变。
  while (!info.exists()) {
    missing.prepend(info.fileName());
    const QString parent = info.absolutePath();
    if (parent == info.absoluteFilePath())
      return absolute;
    info.setFile(parent);
  }
  const QString physical = info.canonicalFilePath();
  return physical.isEmpty()
             ? absolute
             : QDir::cleanPath(QDir(physical).filePath(missing.join('/')));
}
ZzResult<void> writable(const QString &dir) {
  if (!QDir().mkpath(dir))
    return ZzResult<void>::failure(
        error("Cannot create settings directory", dir));
  QTemporaryFile probe(QDir(dir).filePath(".zzrk-write-XXXXXX"));
  if (!probe.open() || probe.write("probe") != 5 || !probe.flush())
    return ZzResult<void>::failure(
        error("Settings directory is not writable", dir));
  return ZzResult<void>::success();
}
ZzResult<void> atomicWrite(const QString &path, const QByteArray &bytes) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit())
    return ZzResult<void>::failure(error("Cannot commit settings file", path));
  return ZzResult<void>::success();
}
ZzResult<QVariantMap> readData(const QString &path) {
  if (!QFileInfo(path).isFile())
    return ZzResult<QVariantMap>::failure(
        error("Settings file is missing", path));
  QSettings syntax(path, QSettings::IniFormat);
  const auto keys = syntax.allKeys();
  if (syntax.status() != QSettings::NoError || !keys.contains("schemaVersion"))
    return ZzResult<QVariantMap>::failure(
        error("Malformed settings file", path));
  ZzCore::ZzQtSettingsStore backend(path);
  auto version = backend.read(u"schemaVersion");
  if (!version)
    return ZzResult<QVariantMap>::failure(version.error());
  bool valid = false;
  const int schema = version.value().toInt(&valid);
  if (!valid || schema != 1)
    return ZzResult<QVariantMap>::failure(
        error("Unsupported settings schema", path));
  auto mapMetadata = backend.read(u"meta/jsonMapKeys", QStringList{});
  if (!mapMetadata)
    return ZzResult<QVariantMap>::failure(mapMetadata.error());
  const auto mapKeys = mapMetadata.value().toStringList();
  auto data = defaults();
  for (const auto &key : keys) {
    if (key == "schemaVersion" || key == "meta/jsonMapKeys")
      continue;
    auto stored = backend.read(QStringView(key));
    if (!stored)
      return ZzResult<QVariantMap>::failure(stored.error());
    QVariant value = stored.value();
    if (mapKeys.contains(key)) {
      QJsonParseError parseError;
      const auto json =
          QJsonDocument::fromJson(value.toString().toUtf8(), &parseError);
      if (parseError.error != QJsonParseError::NoError || !json.isObject())
        return ZzResult<QVariantMap>::failure(
            error("Invalid JSON settings map", path + ":" + key));
      value = json.toVariant().toMap();
    }
    data.insert(key, normalized(key, value));
  }
  return ZzResult<QVariantMap>::success(data);
}
ZzResult<void> saveData(const QString &path, const QVariantMap &data) {
  // 正式 INI 由框架后端序列化，最后原子替换，失败时保留原文件。
  QTemporaryDir staging;
  if (!staging.isValid())
    return ZzResult<void>::failure(error("Cannot stage settings", path));
  const QString temporary = staging.filePath("settings.ini");
  {
    ZzCore::ZzQtSettingsStore backend(temporary);
    auto result = backend.write(u"schemaVersion", 1);
    if (!result)
      return result;
    QStringList mapKeys;
    for (auto it = data.cbegin(); it != data.cend(); ++it) {
      QVariant value = it.value();
      if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
        // JSON 保留业务映射的可读性；元数据区分普通字符串和映射。
        mapKeys.append(it.key());
        value = QString::fromUtf8(
            QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
      }
      result = backend.write(QStringView(it.key()), value);
      if (!result)
        return result;
    }
    result = backend.write(u"meta/jsonMapKeys", mapKeys);
    if (!result)
      return result;
    result = backend.sync();
    if (!result)
      return result;
  }
  QFile serialized(temporary);
  if (!serialized.open(QIODevice::ReadOnly))
    return ZzResult<void>::failure(error("Cannot read staged settings", path));
  return atomicWrite(path, serialized.readAll());
}
} // namespace

GuiSettingsStore::GuiSettingsStore(QString user, QString app)
    : userDirectory_(cleanDirectory(user)),
      applicationDirectory_(cleanDirectory(app)), data_(defaults()) {}
QString GuiSettingsStore::filePath() const {
  return directory_.isEmpty() ? QString{}
                              : QDir(directory_).filePath("settings.ini");
}
QVariant GuiSettingsStore::value(QString key, QVariant fallback) const {
  return data_.value(key, fallback);
}
void GuiSettingsStore::setValue(QString key, QVariant value) {
  if (!key.isEmpty() && key != "schemaVersion" && key != "meta/jsonMapKeys")
    data_.insert(key, normalized(key, value));
}
ZzResult<bool> GuiSettingsStore::locate() {
  const QString marker =
      QDir(applicationDirectory_).filePath("zzrk-portable.ini");
  const QString locator = QDir(userDirectory_).filePath("startup.ini");
  GuiSettingsLocation location;
  QString dir;
  if (QFileInfo::exists(marker)) {
    QSettings settings(marker, QSettings::IniFormat);
    if (settings.value("schemaVersion").toInt() != 1 ||
        settings.status() != QSettings::NoError)
      return ZzResult<bool>::failure(error("Invalid portable marker", marker));
    location = GuiSettingsLocation::Application;
    dir = applicationDirectory_;
  } else if (QFileInfo::exists(locator)) {
    QSettings settings(locator, QSettings::IniFormat);
    const QString saved = settings.value("location").toString();
    if (settings.value("schemaVersion").toInt() != 1 ||
        settings.status() != QSettings::NoError)
      return ZzResult<bool>::failure(
          error("Invalid settings locator", locator));
    if (saved == "user") {
      location = GuiSettingsLocation::User;
      dir = userDirectory_;
    } else if (saved == "application") {
      location = GuiSettingsLocation::Application;
      dir = applicationDirectory_;
    } else if (saved == "custom") {
      location = GuiSettingsLocation::Custom;
      dir = settings.value("directory").toString();
    } else
      return ZzResult<bool>::failure(
          error("Invalid settings location", locator));
    if (!QDir::isAbsolutePath(dir))
      return ZzResult<bool>::failure(
          error("Invalid settings directory", locator));
  } else
    return ZzResult<bool>::success(false);
  auto loaded = readData(QDir(dir).filePath("settings.ini"));
  if (!loaded)
    return ZzResult<bool>::failure(loaded.error());
  auto probe = writable(dir);
  if (!probe)
    return ZzResult<bool>::failure(probe.error());
  data_ = loaded.value();
  directory_ = cleanDirectory(dir);
  location_ = location;
  return ZzResult<bool>::success(true);
}
ZzResult<void> GuiSettingsStore::writeLocator(GuiSettingsLocation location,
                                              const QString &dir) {
  locatorUncertain_ = false;
  const QString marker =
      QDir(applicationDirectory_).filePath("zzrk-portable.ini");
  // 便携模式只依赖应用目录标记，不触碰可能不可写的用户目录。
  if (location == GuiSettingsLocation::Application)
    return atomicWrite(marker, "schemaVersion=1\n");
  auto probe = writable(userDirectory_);
  if (!probe)
    return probe;
  const QString locator = QDir(userDirectory_).filePath("startup.ini");
  QByteArray previous;
  const bool existed = QFileInfo::exists(locator);
  if (existed) {
    QFile file(locator);
    if (!file.open(QIODevice::ReadOnly))
      return ZzResult<void>::failure(error("Cannot read locator", locator));
    previous = file.readAll();
  }
  QTemporaryDir staging;
  if (!staging.isValid())
    return ZzResult<void>::failure(error("Cannot stage locator", locator));
  const QString temporary = staging.filePath("startup.ini");
  {
    QSettings settings(temporary, QSettings::IniFormat);
    settings.setValue("schemaVersion", 1);
    settings.setValue("location", location == GuiSettingsLocation::User ? "user"
                                  : location == GuiSettingsLocation::Application
                                      ? "application"
                                      : "custom");
    settings.setValue("directory", dir);
    settings.sync();
    if (settings.status() != QSettings::NoError)
      return ZzResult<void>::failure(
          error("Cannot serialize locator", locator));
  }
  QFile file(temporary);
  if (!file.open(QIODevice::ReadOnly))
    return ZzResult<void>::failure(
        error("Cannot read staged locator", locator));
  auto result = atomicWrite(locator, file.readAll());
  if (!result)
    return result;
  if (QFileInfo::exists(marker) && !QFile::remove(marker))
    result =
        ZzResult<void>::failure(error("Cannot remove portable marker", marker));
  if (!result) {
    if (existed) {
      const auto restored = atomicWrite(locator, previous);
      if (!restored) {
        locatorUncertain_ = true;
        return restored;
      }
    } else if (!QFile::remove(locator)) {
      locatorUncertain_ = true;
      return ZzResult<void>::failure(
          error("Cannot roll back locator", locator));
    }
  }
  return result;
}
ZzResult<void> GuiSettingsStore::select(GuiSettingsLocation location,
                                        const QString &customDir,
                                        bool migrate) {
  if (migrate && directory_.isEmpty())
    return ZzResult<void>::failure(error("Settings not initialized"));
  if (location == GuiSettingsLocation::Custom &&
      !QDir::isAbsolutePath(customDir))
    return ZzResult<void>::failure(
        error("Custom settings path must be absolute", customDir));
  QString dir = location == GuiSettingsLocation::User ? userDirectory_
                : location == GuiSettingsLocation::Application
                    ? applicationDirectory_
                    : customDir;
  dir = cleanDirectory(dir);
  if (dir == directory_) {
    if (location == location_)
      return ZzResult<void>::success();
    auto current = readData(filePath());
    if (!current)
      return ZzResult<void>::failure(current.error());
    auto result = writeLocator(location, dir);
    if (result)
      location_ = location;
    return result;
  }
  const QString target = QDir(dir).filePath("settings.ini");
  const bool existed = QFileInfo::exists(target);
  if (existed && migrate)
    return ZzResult<void>::failure(
        error("Destination already contains settings", target));
  QVariantMap next = migrate ? data_ : defaults();
  if (existed) {
    auto loaded = readData(target);
    if (!loaded)
      return ZzResult<void>::failure(loaded.error());
    next = loaded.value();
  }
  auto result = writable(dir);
  if (!result)
    return result;
  if (!existed) {
    result = saveData(target, next);
    if (!result)
      return result;
  }
  result = writeLocator(location, dir);
  if (!result) {
    // 定位回滚失败时保留目标，避免定位仍指向它却删除唯一可读配置。
    if (!existed && !locatorUncertain_ && !QFile::remove(target))
      return ZzResult<void>::failure(
          error("Cannot roll back destination", target));
    return result;
  }
  directory_ = dir;
  location_ = location;
  data_ = next;
  return ZzResult<void>::success();
}
ZzResult<void> GuiSettingsStore::initialize(GuiSettingsLocation location,
                                            QString customDir) {
  return select(location, customDir, false);
}
ZzResult<void> GuiSettingsStore::relocate(GuiSettingsLocation location,
                                          QString customDir) {
  return select(location, customDir, true);
}
ZzResult<void> GuiSettingsStore::sync() {
  if (directory_.isEmpty())
    return ZzResult<void>::failure(error("Settings not initialized"));
  // 外部程序升级或损坏配置后，也不允许旧进程覆盖未知格式。
  const auto existing = readData(filePath());
  if (!existing)
    return ZzResult<void>::failure(existing.error());
  return saveData(filePath(), data_);
}
ZzResult<void> GuiSettingsStore::reset() {
  const auto previous = data_;
  data_ = defaults();
  auto result = sync();
  if (!result)
    data_ = previous;
  return result;
}
} // namespace zzrk::gui
