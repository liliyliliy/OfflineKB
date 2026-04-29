#include "document.h"
#include "searchengine.h"

#include <QCoreApplication>
#include <QDir>
#include <QString>

#include <cassert>
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    // 让 QCoreApplication 可用，方便获取可执行文件路径
    QCoreApplication app(argc, argv);

    // 按当前工程结构推导词典目录：build 下运行 -> ../resources/dict
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString dictDir = QDir(appDir).absoluteFilePath("../resources/dict");

    SearchEngine engine(QDir::toNativeSeparators(dictDir).toStdString());

    Document d1;
    d1.id = 1;
    d1.title = QString::fromUtf8("Qt入门");
    d1.content = QString::fromUtf8("Qt Widgets 开发 桌面 应用");

    Document d2;
    d2.id = 2;
    d2.title = QString::fromUtf8("SQLite教程");
    d2.content = QString::fromUtf8("SQLite 数据库 检索 引擎");

    std::vector<Document> docs{d1, d2};
    engine.buildIndex(docs);

    const auto results = engine.search("数据库 检索");
    assert(!results.empty());
    assert(results.front().first == 2);

    std::cout << "test_search passed" << std::endl;
    return 0;
}