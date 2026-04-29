#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QStringList>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 拷贝词典到临时目录（用于调试/兜底）
    const QString tmpDir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/OfflineKB_dict/";
    QDir().mkpath(tmpDir);

    const QStringList dicts = {
        "jieba.dict.utf8", "hmm_model.utf8", "user.dict.utf8", "idf.utf8", "stop_words.utf8"};

    for (const QString& f : dicts) {
        const QString src = ":/dict/" + f;
        const QString dst = tmpDir + f;

        // 如果目标已存在，先删除再拷贝，避免 copy 因文件已存在失败
        if (QFile::exists(dst)) {
            QFile::remove(dst);
        }
        QFile::copy(src, dst);
    }

    MainWindow window;
    window.show();

    return app.exec();
}