#include "GuiSettings.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
using namespace zzrk::gui;
namespace {
int failures = 0;
void expect(bool ok, const char *description) {
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
  }
}
void writeFile(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  expect(file.open(QIODevice::WriteOnly), "fixture opens");
  expect(file.write(bytes) == bytes.size(), "fixture writes");
}
void runSettingsTests(const QString &root) {
  QTemporaryDir temp(root + "/settings-XXXXXX");
  const QString user = temp.filePath("user"),
                application = temp.filePath("app"),
                custom = temp.filePath("custom");
  GuiSettingsStore store(user, application);
  auto first = store.locate();
  expect(first && !first.value(), "first run requires selection");
  expect(!QFile::exists(user + "/settings.ini"), "first run writes nothing");
  expect(store.initialize(GuiSettingsLocation::User).hasValue(),
         "initialize user location");
  expect(store.directory() == QFileInfo(user).canonicalFilePath(),
         "created user location uses physical directory");
  const QVariantMap draft{{"boot", "/tmp/镜像.img"}, {"enabled", true}};
  store.setValue("draft/files", draft);
  store.setValue("appearance/theme", "dark");
  expect(store.sync().hasValue(), "sync writes settings");
  {
    QSettings readable(store.filePath(), QSettings::IniFormat);
    expect(readable.value("appearance/theme").toString() == "dark",
           "INI exposes editable appearance theme");
    expect(!readable.contains("data"), "INI has no opaque all-data blob");
    const auto json = QJsonDocument::fromJson(
        readable.value("draft/files").toString().toUtf8());
    expect(json.isObject() && json.toVariant().toMap() == draft,
           "INI maps are readable JSON objects");
  }
  GuiSettingsStore reopened(user, application);
  auto found = reopened.locate();
  expect(found && found.value(), "restart locates existing configuration");
  expect(reopened.value("draft/files").toMap() == draft,
         "nested map roundtrip");
  expect(reopened.value("appearance/theme").toString() == "dark",
         "theme roundtrip");
  expect(reopened.relocate(GuiSettingsLocation::Custom, custom).hasValue(),
         "relocate custom");
  expect(QFile::exists(user + "/settings.ini"), "migration retains source");
  GuiSettingsStore migrated(user, application);
  found = migrated.locate();
  expect(found && found.value() &&
             migrated.directory() == QFileInfo(custom).canonicalFilePath(),
         "locator switches after save");
  expect(migrated.value("draft/files").toMap() == draft,
         "migration preserves drafts");
  writeFile(temp.filePath("blocked"), "file");
  expect(!migrated.relocate(GuiSettingsLocation::Custom,
                            temp.filePath("blocked/child")),
         "target failure reported");
  expect(migrated.directory() == QFileInfo(custom).canonicalFilePath(),
         "target failure retains active location");
  expect(!migrated.relocate(GuiSettingsLocation::Custom, "relative"),
         "custom requires absolute path");
  expect(!migrated.relocate(GuiSettingsLocation::User),
         "existing destination rejected");
  expect(migrated.reset().hasValue(), "reset saves defaults");
  expect(migrated.directory() == QFileInfo(custom).canonicalFilePath() &&
             !migrated.value("draft/files").isValid(),
         "reset keeps directory but clears draft");
  migrated.setValue("appearance/theme", "broken");
  migrated.setValue("upgrade/verify", "broken");
  expect(migrated.value("appearance/theme").toString() == "system" &&
             migrated.value("upgrade/verify").toString() == "critical",
         "invalid enums fall back");
  expect(migrated.relocate(GuiSettingsLocation::Application).hasValue(),
         "enable portable location");
  const QString moved = temp.filePath("moved");
  expect(QDir().rename(application, moved),
         "move portable application directory");
  GuiSettingsStore portable(user, moved);
  found = portable.locate();
  expect(found && found.value() &&
             portable.directory() == QFileInfo(moved).canonicalFilePath(),
         "portable marker follows moved application and beats stale locator");
  const QString bad = temp.filePath("bad");
  QDir().mkpath(bad);
  writeFile(bad + "/settings.ini", "schemaVersion=99\ndata=unused\n");
  expect(!portable.initialize(GuiSettingsLocation::Custom, bad),
         "unknown schema rejected");
  QFile untouched(bad + "/settings.ini");
  expect(untouched.open(QIODevice::ReadOnly), "unknown fixture opens");
  expect(untouched.readAll().startsWith("schemaVersion=99"),
         "unknown schema remains untouched");
  writeFile(bad + "/settings.ini", "[unterminated\n");
  expect(!portable.initialize(GuiSettingsLocation::Custom, bad),
         "malformed config rejected");
  // 定位文件失败发生在目标配置写入之后，不能使活动目录发生变化。
  QFile::remove(user + "/startup.ini");
  QDir().mkdir(user + "/startup.ini");
  const QString failed = temp.filePath("failed");
  expect(!portable.relocate(GuiSettingsLocation::Custom, failed),
         "locator commit failure reported");
  expect(portable.directory() == QFileInfo(moved).canonicalFilePath() &&
             !QFile::exists(failed + "/settings.ini"),
         "locator failure rolls back target and active location");
  writeFile(moved + "/settings.ini", "schemaVersion=99\ndata=unused\n");
  expect(!portable.sync(), "external future schema prevents overwrite by sync");
  // 捕获初始化已有目录时误重置配置，以及读取异常枚举不归一化的回归。
  QTemporaryDir previous(root + "/previous-XXXXXX");
  GuiSettingsStore original(previous.filePath("user"),
                            previous.filePath("app"));
  expect(
      original
          .initialize(GuiSettingsLocation::Custom, previous.filePath("custom"))
          .hasValue(),
      "create reusable custom folder");
  original.setValue("draft/files", draft);
  expect(original.sync().hasValue(), "save reusable draft");
  {
    QSettings external(original.filePath(), QSettings::IniFormat);
    external.setValue("appearance/language", "invalid");
    external.sync();
  }
  GuiSettingsStore selected(previous.filePath("another-user"),
                            previous.filePath("another-app"));
  expect(
      selected
          .initialize(GuiSettingsLocation::Custom, previous.filePath("custom"))
          .hasValue(),
      "select valid preexisting folder");
  expect(selected.value("draft/files").toMap() == draft,
         "selection preserves preexisting draft");
  expect(selected.value("appearance/language").toString() == "system",
         "load normalizes invalid language");
  expect(selected
             .relocate(GuiSettingsLocation::Custom,
                       selected.directory() + "/../custom")
             .hasValue(),
         "same canonical directory migration is a no-op");
  // 同一物理目录切换模式也必须更新便携标记，不能误当迁移无操作。
  QTemporaryDir modes(root + "/modes-XXXXXX");
  const QString modeApp = modes.filePath("app"),
                modeUser = modes.filePath("user");
  GuiSettingsStore switching(modeUser, modeApp);
  expect(switching.initialize(GuiSettingsLocation::Custom, modeApp).hasValue(),
         "custom application folder initialized");
  const QString physicalApp = QFileInfo(modeApp).canonicalFilePath();
  expect(switching.directory() == physicalApp &&
             switching.applicationDirectory() == physicalApp,
         "application path stays physical after custom folder creation");
  switching.setValue("appearance/theme", "dark");
  expect(switching.sync().hasValue(), "same folder draft saved");
  expect(switching.relocate(GuiSettingsLocation::Application).hasValue(),
         "same folder switches to portable");
  expect(QFile::exists(modeApp + "/zzrk-portable.ini"),
         "same folder portable switch creates marker");
  GuiSettingsStore inPortable(modeUser, modeApp);
  auto modeLocated = inPortable.locate();
  expect(modeLocated && modeLocated.value() &&
             inPortable.location() == GuiSettingsLocation::Application &&
             inPortable.directory() == switching.directory(),
         "restart uses portable mode after same-folder switch");
  expect(switching.relocate(GuiSettingsLocation::Custom, modeApp).hasValue(),
         "same folder switches back to custom");
  expect(!QFile::exists(modeApp + "/zzrk-portable.ini"),
         "same folder custom switch removes marker");
  GuiSettingsStore inCustom(modeUser, modeApp);
  modeLocated = inCustom.locate();
  expect(modeLocated && modeLocated.value() &&
             inCustom.location() == GuiSettingsLocation::Custom &&
             inCustom.value("appearance/theme").toString() == "dark",
         "restart retains custom mode and data after same-folder switch");
  QTemporaryDir blockedUser(root + "/blocked-user-XXXXXX");
  writeFile(blockedUser.filePath("user-file"), "blocked");
  GuiSettingsStore fullyPortable(blockedUser.filePath("user-file/child"),
                                 blockedUser.filePath("app"));
  expect(fullyPortable.initialize(GuiSettingsLocation::Application).hasValue(),
         "portable initialization does not require writable user directory");
  GuiSettingsStore portableRestart(blockedUser.filePath("user-file/child"),
                                   blockedUser.filePath("app"));
  auto portableLocated = portableRestart.locate();
  expect(portableLocated && portableLocated.value() &&
             portableRestart.location() == GuiSettingsLocation::Application,
         "fully portable restart uses application marker alone");
  const QString disappeared = modes.filePath("disappeared");
  expect(QDir().rename(modeApp, disappeared),
         "active custom folder moved away");
  expect(!switching.sync(), "missing active config refuses sync");
  expect(!QDir(modeApp).exists(),
         "sync does not recreate missing active directory");
  QTemporaryDir markerFailure(root + "/marker-failure-XXXXXX");
  const QString failureApp = markerFailure.filePath("app"),
                failureUser = markerFailure.filePath("user");
  GuiSettingsStore markerStore(failureUser, failureApp);
  expect(markerStore.initialize(GuiSettingsLocation::User).hasValue(),
         "marker failure fixture initialized");
  expect(QDir().mkpath(failureApp + "/zzrk-portable.ini"),
         "marker path blocked by directory");
  writeFile(failureApp + "/zzrk-portable.ini/child", "keep");
  expect(!markerStore.relocate(GuiSettingsLocation::Application),
         "portable marker commit failure reported");
  expect(
      markerStore.directory() == QFileInfo(failureUser).canonicalFilePath() &&
          !QFile::exists(failureApp + "/settings.ini"),
      "failed portable marker retains source and removes staged destination");
  {
    QSettings locator(failureUser + "/startup.ini", QSettings::IniFormat);
    expect(locator.value("location").toString() == "user",
           "failed portable marker does not change user locator");
  }
  const QString rejected = markerFailure.filePath("custom");
  expect(!markerStore.relocate(GuiSettingsLocation::Custom, rejected),
         "marker removal failure reported");
  expect(markerStore.directory() == QFileInfo(failureUser).canonicalFilePath() &&
             !QFile::exists(rejected + "/settings.ini"),
         "successful locator rollback keeps source and removes unused "
         "destination");
  {
    QSettings locator(failureUser + "/startup.ini", QSettings::IniFormat);
    expect(locator.value("location").toString() == "user" &&
               locator.value("directory").toString() ==
                   QFileInfo(failureUser).canonicalFilePath(),
           "marker removal failure restores previous locator");
  }
}
} // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  expect(root.isValid(), "test root created");
  runSettingsTests(root.path());
#ifdef Q_OS_UNIX
  // 模拟 macOS /var → /private/var：所有尚未创建的设置目录都经过别名。
  QTemporaryDir aliases;
  const QString alias = aliases.filePath("alias");
  const bool linked = QFile::link(root.path(), alias);
  expect(linked, "directory symlink fixture created");
  if (linked)
    runSettingsTests(alias);
#endif
  return failures;
}
