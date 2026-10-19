#include <QCoreApplication>
#include <QFile>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "RepoFilePreview.h"

namespace qZypper {

namespace {

/**
 * @brief errnoを英語のユーザ向け文言へ変換する
 * @param errnoValue 変換するerrno値
 * @return 文言
 */
QString errorString(int errnoValue)
{
    return QString::fromLocal8Bit(std::strerror(errnoValue));
}

} // namespace

/**
 * @brief ユーザ指定の.repoファイルをプレビュー用に安全に読み込む
 *
 * O_NONBLOCKで開き、fstatで通常ファイルであることを確認したうえで上限バイト数まで読み込む
 *
 * 名前付きパイプやデバイス、ディレクトリ、上限を超えるファイルは拒否する
 *
 * FUSE等でread自体がブロックし得るため、呼出し側はワーカースレッドで実行すること
 *
 * @param path 読み込むファイルのパス
 * @param maxSize 許可する最大バイト数
 * @param content 成功時の出力先 (ファイル内容そのままのバイト列)
 * @param error 失敗時の理由 (英語のユーザ向け文言)
 * @return 成功時true
 */
bool readRepoFileForPreview(const QString &path, qint64 maxSize, QByteArray &content, QString &error)
{
    content.clear();
    error.clear();

    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    if (fd < 0) {
        error = QCoreApplication::translate("qZypper::PackageController", "Cannot open file: %1")
                    .arg(errorString(errno));
        return false;
    }

    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        error = QCoreApplication::translate("qZypper::PackageController", "Cannot read file: %1")
                    .arg(errorString(errno));
        ::close(fd);
        return false;
    }
    // 名前付きパイプ・デバイス・ディレクトリはreadで停止し得るため、通常ファイル以外は拒否する
    if (!S_ISREG(st.st_mode)) {
        error = QCoreApplication::translate("qZypper::PackageController",
                                            "The selected item is not a regular file");
        ::close(fd);
        return false;
    }
    if (st.st_size < 0 || static_cast<qint64>(st.st_size) > maxSize) {
        error = QCoreApplication::translate("qZypper::PackageController",
                                            "The file is too large (maximum 64 KiB)");
        ::close(fd);
        return false;
    }

    // サイズ確認後に成長する場合に備え、上限を超えた時点で打ち切る
    QByteArray buffer;
    char chunk[8192];
    qint64 total = 0;
    for (;;) {
        // 上限+1バイトを超えて要求しない (超過の検出に必要な分だけ読む)
        const size_t want = static_cast<size_t>(
            std::min<qint64>(static_cast<qint64>(sizeof(chunk)), maxSize + 1 - total));
        const ssize_t n = ::read(fd, chunk, want);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            error = QCoreApplication::translate("qZypper::PackageController", "Cannot read file: %1")
                        .arg(errorString(errno));
            ::close(fd);
            return false;
        }
        if (n == 0)
            break;
        total += static_cast<qint64>(n);
        if (total > maxSize) {
            error = QCoreApplication::translate("qZypper::PackageController",
                                                "The file is too large (maximum 64 KiB)");
            ::close(fd);
            return false;
        }
        buffer.append(chunk, static_cast<int>(n));
    }
    ::close(fd);

    content = buffer;
    return true;
}

} // namespace qZypper
