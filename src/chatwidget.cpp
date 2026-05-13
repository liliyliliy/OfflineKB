#include "chatwidget.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QString>
#include <QTextCursor>
#include <QTextBrowser>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

ChatWidget::ChatWidget(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    chatHistory_ = new QTextEdit(this);
    chatHistory_->setReadOnly(true);
    chatHistory_->setAcceptRichText(true);
    chatHistory_->setObjectName("chatHistory");

    auto* sourcesLabel = new QLabel(tr("本轮参考来源"), this);
    sourcesLabel->setObjectName("sourcesLabel");

    sourcesView_ = new QTextBrowser(this);
    sourcesView_->setReadOnly(true);
    sourcesView_->setAcceptRichText(true);
    sourcesView_->setFixedHeight(86);
    sourcesView_->setObjectName("sourcesView");
    sourcesView_->setOpenExternalLinks(false);
    sourcesView_->setOpenLinks(false);

    connect(sourcesView_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        // url 格式: source:N，N 为来源 entry 下标
        // 不用 source://N 是因为纯数字不是合法的 hostname，会被 QUrl 丢弃
        if (url.scheme() == QStringLiteral("source")) {
            bool ok = false;
            int idx = url.path().toInt(&ok);
            if (ok) {
                emit sourceClicked(idx);
            }
        }
    });

    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(6);

    inputEdit_ = new QLineEdit(this);
    inputEdit_->setPlaceholderText(tr("请输入您的问题，回车发送..."));
    inputEdit_->setClearButtonEnabled(true);
    inputEdit_->setObjectName("inputEdit");

    sendBtn_ = new QPushButton(tr("发送"), this);
    sendBtn_->setObjectName("sendBtn");
    sendBtn_->setDefault(true);

    bottom->addWidget(inputEdit_, /*stretch=*/1);
    bottom->addWidget(sendBtn_,   /*stretch=*/0);

    root->addWidget(chatHistory_, /*stretch=*/1);
    root->addWidget(sourcesLabel, /*stretch=*/0);
    root->addWidget(sourcesView_, /*stretch=*/0);
    root->addLayout(bottom);

    // 信号槽：按钮点击 / 输入框回车 → onSendClicked
    connect(sendBtn_, &QPushButton::clicked, this, &ChatWidget::onSendClicked);
    connect(inputEdit_, &QLineEdit::returnPressed, this, &ChatWidget::onSendClicked);
}

void ChatWidget::onSendClicked() {
    if (!inputEdit_) {
        return;
    }

    const QString text = inputEdit_->text().trimmed();
    if (text.isEmpty()) {
        return;
    }

    inputEdit_->clear();

    // 仅向上层通知，由上层决定如何回显与如何调用 RAG 引擎
    emit sendMessage(text);
}

void ChatWidget::appendMessage(const QString& sender, const QString& text) {
    if (!chatHistory_) {
        return;
    }

    const QString safeSender = escapeHtml(sender);
    const QString body = renderMarkdown(text);

    // 每条消息一行 HTML：发送者加粗 + 冒号 + 正文 + 末尾换行
    QString html;
    html.reserve(safeSender.size() + body.size() + 32);
    html += QStringLiteral("<p style=\"margin:2px 0;\"><b>");
    html += safeSender;
    html += QStringLiteral("：</b> ");
    html += body;
    html += QStringLiteral("</p>");

    chatHistory_->append(html);

    // 自动滚动到底部
    if (auto* sb = chatHistory_->verticalScrollBar()) {
        sb->setValue(sb->maximum());
    }
}

void ChatWidget::setSources(const QString& sources) {
    if (!sourcesView_) {
        return;
    }

    const QString trimmed = sources.trimmed();
    if (trimmed.isEmpty()) {
        sourcesView_->setHtml(QStringLiteral(
            "<span style=\"color:#666;\">暂无参考来源</span>"));
        return;
    }

    // sources 已是 HTML，直接设置
    sourcesView_->setHtml(trimmed);
}

QString ChatWidget::renderMarkdown(const QString& text) const {
    QString safe = escapeHtml(text);

    // **加粗** → <b>加粗</b>
    static const QRegularExpression boldRe(
        QStringLiteral("\\*\\*([^*\\n]+)\\*\\*"));
    safe.replace(boldRe, QStringLiteral("<b>\\1</b>"));

    // 换行：\r\n / \r / \n 全部统一成 <br/>
    safe.replace(QStringLiteral("\r\n"), QStringLiteral("<br/>"));
    safe.replace(QChar('\r'), QStringLiteral("<br/>"));
    safe.replace(QChar('\n'), QStringLiteral("<br/>"));

    return safe;
}

QString ChatWidget::escapeHtml(const QString& s) {
    QString out;
    out.reserve(s.size());

    for (QChar c : s) {
        switch (c.unicode()) {
            case '&':  out += QStringLiteral("&amp;");  break;
            case '<':  out += QStringLiteral("&lt;");   break;
            case '>':  out += QStringLiteral("&gt;");   break;
            case '"':  out += QStringLiteral("&quot;"); break;
            case '\'': out += QStringLiteral("&#39;");  break;
            default:   out += c;                        break;
        }
    }

    return out;
}
