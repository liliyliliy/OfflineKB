#ifndef OFFLINEKB_CHATWIDGET_H
#define OFFLINEKB_CHATWIDGET_H

// =============================================================================
// ChatWidget
// -----------------------------------------------------------------------------
// 聊天面板：顶部为 QTextEdit 显示历史，底部为 QLineEdit + QPushButton 发送区。
//
// 解耦原则：
//   - 仅通过 sendMessage(QString) 信号把用户输入传递给上层（如 MainWindow）
//   - 不直接持有 RagEngine，也不发起异步推理
//   - appendMessage 槽用于把任意一方（用户 / AI / 系统）的消息追加到历史
//
// Markdown：仅支持 **加粗** 与换行；HTML 转义后再做替换，防止注入。
// =============================================================================

#include <QWidget>

class QLineEdit;
class QPushButton;
class QTextEdit;

class ChatWidget : public QWidget {
    Q_OBJECT

public:
    explicit ChatWidget(QWidget* parent = nullptr);
    ~ChatWidget() override = default;

signals:
    // 用户点击发送或在输入框按下回车时发出
    void sendMessage(const QString& msg);

public slots:
    // 发送按钮槽（通常由内部信号连接，也可被外部主动调用）
    void onSendClicked();

    // 在历史区追加一条消息
    //   sender 显示的发送者名（如 "我" / "AI" / "系统"）
    //   text   消息正文，支持简单 Markdown
    void appendMessage(const QString& sender, const QString& text);

private:
    // 简单 Markdown 渲染：先 HTML 转义，再替换 **xxx** 与换行
    QString renderMarkdown(const QString& text) const;

    // HTML 转义：避免外部文本中的 < > & " ' 影响渲染
    static QString escapeHtml(const QString& s);

    QTextEdit*   chatHistory_ = nullptr;
    QLineEdit*   inputEdit_   = nullptr;
    QPushButton* sendBtn_     = nullptr;
};

#endif  // OFFLINEKB_CHATWIDGET_H
