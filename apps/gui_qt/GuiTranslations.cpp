// SPDX-License-Identifier: MIT
#include "GuiTranslations.hpp"
#include <QCheckBox>
#include <QCoreApplication>
#include <QEvent>
#include <QLabel>
#include <QLocale>
#include <QPointer>
#include <QStatusBar>
#include <QWidget>

// 显式引用资源初始化符号，防止静态库链接时丢弃翻译资源。
static void initializeTranslationResources() {
  Q_INIT_RESOURCE(gui_translations);
}

namespace zzrk::gui {
namespace {
QPointer<GuiTranslations> activeTranslations;

class TranslationBinding final : public QObject {
public:
  TranslationBinding(QObject *target, std::function<void()> refresh)
      : QObject(target), m_refresh(std::move(refresh)) {
    target->installEventFilter(this);
  }
  void replace(std::function<void()> refresh) {
    m_refresh = std::move(refresh);
    m_refresh();
  }

protected:
  bool eventFilter(QObject *target, QEvent *event) override {
    if (event->type() == QEvent::LanguageChange)
      m_refresh();
    return QObject::eventFilter(target, event);
  }

private:
  std::function<void()> m_refresh;
};

void bindProperty(QObject *target, const QString &key,
                  std::function<void()> refresh) {
  // 同一属性只保留一个绑定；状态改变时更新该绑定，而不是累积旧回调。
  for (auto *child : target->children())
    if (child->objectName() == key)
      if (auto *binding = dynamic_cast<TranslationBinding *>(child)) {
        binding->replace(std::move(refresh));
        return;
      }
  refresh();
  auto *binding = new TranslationBinding(target, std::move(refresh));
  binding->setObjectName(key);
}
} // namespace

GuiTranslations::GuiTranslations(QObject *parent) : QObject(parent) {
  initializeTranslationResources();
  activeTranslations = this;
}
GuiTranslations::~GuiTranslations() {
  // QObject 子对象可能在 QApplication 基类析构后释放，此时无需再卸载翻译器。
  if (QCoreApplication::instance() && m_translator)
    QCoreApplication::removeTranslator(m_translator.get());
}
bool GuiTranslations::install(QString language) {
  if (language == QStringLiteral("system")) {
    language = QLocale::system().language() == QLocale::English
                   ? QStringLiteral("en")
                   : QStringLiteral("zh_CN");
  }
  if (language != QStringLiteral("en")) {
    if (m_translator)
      QCoreApplication::removeTranslator(m_translator.get());
    m_translator.reset();
    m_effectiveLanguage = QStringLiteral("zh_CN");
    activeTranslations = this;
    return true;
  }
  // 先验证新资源，失败时继续使用原语言。相同有效语言不重复发送刷新事件。
  if (m_translator && m_effectiveLanguage == QStringLiteral("en")) {
    activeTranslations = this;
    return true;
  }
  auto candidate = std::make_unique<QTranslator>();
  if (!candidate->load(QStringLiteral(":/translations/zzrkdevtool_en.qm")))
    return false;
  if (!QCoreApplication::installTranslator(candidate.get()))
    return false;
  if (m_translator)
    QCoreApplication::removeTranslator(m_translator.get());
  m_translator = std::move(candidate);
  m_effectiveLanguage = QStringLiteral("en");
  activeTranslations = this;
  return true;
}
QString GuiTranslations::effectiveLanguage() const {
  return m_effectiveLanguage;
}
GuiTranslations *GuiTranslations::active() { return activeTranslations; }
GuiText guiText(const char *source) {
  // 源文案复制到绑定中，支持调用者使用临时 UTF-8 缓冲区。
  return GuiText([source = QByteArray(source)] {
    return QCoreApplication::translate("ZzRkDevTool", source.constData());
  });
}
void setGuiText(QObject *target, const char *property, GuiText text) {
  const auto name = QByteArray(property);
  bindProperty(target,
               QStringLiteral("zzrkTranslation_") + QString::fromLatin1(name),
               [target, name, text = std::move(text)] {
                 target->setProperty(name.constData(), text.translated());
               });
}
void setGuiStatus(QStatusBar *target, GuiText text) {
  bindProperty(target, QStringLiteral("zzrkTranslation_statusMessage"),
               [target, text = std::move(text)] {
                 target->showMessage(text.translated());
               });
}
void bindGuiTranslation(QWidget *target, std::function<void()> refresh) {
  refresh();
  new TranslationBinding(target, std::move(refresh));
}
QLabel *guiLabel(GuiText text, QWidget *parent) {
  auto *label = new QLabel(parent);
  setGuiText(label, "text", std::move(text));
  return label;
}
QCheckBox *guiCheckBox(GuiText text, QWidget *parent) {
  auto *box = new QCheckBox(parent);
  setGuiText(box, "text", std::move(text));
  return box;
}
} // namespace zzrk::gui
