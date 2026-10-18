#ifndef QZYPPER_GUI_CONTROLLERS_REPOFILEPREVIEW_H
#define QZYPPER_GUI_CONTROLLERS_REPOFILEPREVIEW_H

#include <QByteArray>
#include <QString>

namespace qZypper {

/**
 * @brief ユーザー指定の .repo ファイルをプレビュー用に安全に読み込む。
 *
 * O_NONBLOCK で開き、fstat で通常ファイルであることを確認したうえで上限バイト数
 * まで読み込む。名前付きパイプやデバイス、ディレクトリ、上限を超えるファイルは
 * 拒否する。FUSE 等で read 自体がブロックし得るため、呼出し側はワーカースレッドで実行すること。
 * @param path 読み込むファイルのパス
 * @param maxSize 許可する最大バイト数
 * @param content 成功時の出力先 (ファイル内容そのままのバイト列)
 * @param error 失敗時の理由 (英語のユーザー向け文言)
 * @return 成功時 true
 */
bool readRepoFileForPreview(const QString &path, qint64 maxSize, QByteArray &content, QString &error);

} // namespace qZypper

#endif // QZYPPER_GUI_CONTROLLERS_REPOFILEPREVIEW_H
