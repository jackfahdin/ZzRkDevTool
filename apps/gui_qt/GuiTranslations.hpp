// SPDX-License-Identifier: MIT
#pragma once
#include <QObject>
#include <QString>
#include <QTranslator>
#include <functional>
#include <memory>
#include <tuple>
#include <utility>

class QWidget;
class QStatusBar;
class QLabel;
class QCheckBox;

namespace zzrk::gui {
// 翻译器必须存活到应用退出；Qt 在安装和卸载时发送 LanguageChange。
class GuiTranslations final : public QObject {
public:
  explicit GuiTranslations(QObject *parent = nullptr);
  ~GuiTranslations() override;
  bool install(QString language);
  QString effectiveLanguage() const;
  static GuiTranslations *active();

private:
  std::unique_ptr<QTranslator> m_translator;
  QString m_effectiveLanguage = QStringLiteral("zh_CN");
};

// 字符串保存当前显示值，回调只保存源文案与参数副本。不要捕获设备或控制器指针。
// 转为普通 QString 适合历史日志；实时控件则保留 GuiText 并交给 setGuiText。
class GuiText : public QString {
public:
  GuiText() = default;
  GuiText(QString text) : QString(std::move(text)) {}
  GuiText(const char *text) : QString(QString::fromUtf8(text)) {}
  explicit GuiText(std::function<QString()> render)
      : QString(render()), m_render(std::move(render)) {}
  QString translated() const { return m_render ? m_render() : QString(*this); }

  template <typename... Args> GuiText arg(Args &&...args) const {
    auto values = std::make_tuple(std::forward<Args>(args)...);
    return GuiText([text = *this, values = std::move(values)] {
      return std::apply(
          [&text](const auto &...value) {
            return text.translated().arg(argument(value)...);
          },
          values);
    });
  }
  GuiText toUpper() const {
    return GuiText([text = *this] { return text.translated().toUpper(); });
  }
  GuiText &operator+=(const QString &suffix) {
    *this =
        GuiText([text = *this, suffix] { return text.translated() + suffix; });
    return *this;
  }

private:
  static QString argument(const GuiText &text) { return text.translated(); }
  template <typename T> static const T &argument(const T &value) {
    return value;
  }
  std::function<QString()> m_render;
};

GuiText guiText(const char *source);
// 绑定随目标销毁；重复写同一属性会替换旧文案，避免语言切换覆盖最新状态。
void setGuiText(QObject *target, const char *property, GuiText text);
void setGuiStatus(QStatusBar *target, GuiText text);
void bindGuiTranslation(QWidget *target, std::function<void()> refresh);
QLabel *guiLabel(GuiText text, QWidget *parent);
QCheckBox *guiCheckBox(GuiText text, QWidget *parent);
} // namespace zzrk::gui
