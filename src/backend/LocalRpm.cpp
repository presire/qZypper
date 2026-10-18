#include "LocalRpm.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace qZypper {
namespace {

/**
 * @brief パスの basename を取り出す ('/' 区切り)。
 * @param path 対象パス
 * @return basename
 */
std::string baseName(const std::string &path)
{
    const std::size_t separator = path.find_last_of('/');
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

} // namespace

/**
 * @brief 検証済みローカル RPM の記述子を解放する。
 */
LocalRpmFile::~LocalRpmFile()
{
    if (fd >= 0)
        ::close(fd);
}

/**
 * @brief 記述子の所有権を移す。
 * @param other 移動元
 */
LocalRpmFile::LocalRpmFile(LocalRpmFile &&other) noexcept
    : fd(other.fd), path(std::move(other.path)),
      filename(std::move(other.filename)), size(other.size)
{
    other.fd = -1;
}

/**
 * @brief 記述子の所有権を移す。
 * @param other 移動元
 * @return 自身への参照
 */
LocalRpmFile &LocalRpmFile::operator=(LocalRpmFile &&other) noexcept
{
    if (this != &other) {
        if (fd >= 0)
            ::close(fd);
        fd = other.fd;
        path = std::move(other.path);
        filename = std::move(other.filename);
        size = other.size;
        other.fd = -1;
    }
    return *this;
}

/**
 * @brief パス文字列の構文検査 (絶対パス・'..' 禁止・制御文字禁止・.rpm 拡張子)。
 * @param path 検証対象パス
 * @param err 失敗時の理由
 * @return 有効時 true
 */
bool validateLocalRpmPath(const std::string &path, std::string &err)
{
    if (path.empty()) {
        err = "empty path";
        return false;
    }
    if (path[0] != '/') {
        err = "path must be absolute";
        return false;
    }
    for (unsigned char c : path) {
        if (c < 0x20 || c == 0x7f) {
            err = "path contains control characters";
            return false;
        }
    }
    // 構成要素単位で '..' を拒否する (部分文字列 "..abc" は許容)。
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string component = path.substr(start, end == std::string::npos
                                                          ? std::string::npos : end - start);
        if (component == "..") {
            err = "path must not contain '..'";
            return false;
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    const std::string name = baseName(path);
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".rpm") != 0) {
        err = "file must have a .rpm extension";
        return false;
    }
    return true;
}

/**
 * @brief fstat の結果から呼び出し元が読めるか判定する。
 * @param mode ファイルモード
 * @param fileUid ファイル所有者 uid
 * @param callerUid 呼び出し元 uid
 * @return 読める場合 true
 */
bool isCallerReadable(mode_t mode, uid_t fileUid, uid_t callerUid)
{
    if ((mode & S_IROTH) != 0)
        return true;
    return fileUid == callerUid;
}

/**
 * @brief パスを安全に開いて検証し、LocalRpmFile を返す。
 * @param path 検証対象パス
 * @param callerUid 呼び出し元 uid
 * @param maxBytes 許可最大サイズ
 * @param out 成功時の出力先
 * @param err 失敗時の理由
 * @return 成功時 true
 */
bool openLocalRpm(const std::string &path, uid_t callerUid,
                  std::uintmax_t maxBytes, LocalRpmFile &out, std::string &err)
{
    if (!validateLocalRpmPath(path, err))
        return false;
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        err = std::string("cannot open file: ") + std::strerror(errno);
        return false;
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        err = std::string("cannot stat file: ") + std::strerror(errno);
        ::close(fd);
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        err = "not a regular file";
        ::close(fd);
        return false;
    }
    if (st.st_size < 0 || static_cast<std::uintmax_t>(st.st_size) > maxBytes) {
        err = "file exceeds the size limit";
        ::close(fd);
        return false;
    }
    if (!isCallerReadable(st.st_mode, st.st_uid, callerUid)) {
        err = "file is not readable by the requesting user";
        ::close(fd);
        return false;
    }
    out.fd = fd;
    out.path = path;
    out.filename = baseName(path);
    out.size = static_cast<std::uintmax_t>(st.st_size);
    return true;
}

/**
 * @brief 呼び出し元が開いて渡した記述子を検証して取り込む。
 * @param fd D-Bus で受け取った記述子
 * @param fileName 表示・配置用ファイル名
 * @param maxBytes 許可最大サイズ
 * @param out 成功時の出力先
 * @param err 失敗時の理由
 * @return 成功時 true
 */
bool adoptLocalRpmFd(int fd, const std::string &fileName, std::uintmax_t maxBytes,
                     LocalRpmFile &out, std::string &err)
{
    if (fileName.empty() || fileName.size() > 255 || fileName.front() == '.'
        || fileName.find('/') != std::string::npos) {
        err = "invalid file name";
        return false;
    }
    for (const unsigned char c : fileName) {
        if (c < 0x20 || c == 0x7f) {
            err = "file name contains control characters";
            return false;
        }
    }
    if (fileName.size() < 5 || fileName.compare(fileName.size() - 4, 4, ".rpm") != 0) {
        err = "file must have a .rpm extension";
        return false;
    }
    if (fd < 0) {
        err = "invalid file descriptor";
        return false;
    }
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0 || (flags & O_ACCMODE) == O_WRONLY) {
        err = "file descriptor is not readable";
        return false;
    }
    const int own = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (own < 0) {
        err = std::string("cannot duplicate file descriptor: ") + std::strerror(errno);
        return false;
    }
    struct stat st {};
    if (::fstat(own, &st) != 0) {
        err = std::string("cannot stat file: ") + std::strerror(errno);
        ::close(own);
        return false;
    }
    // FIFO・デバイス等は read で停止し得るため、通常ファイル以外は読む前に拒否する
    if (!S_ISREG(st.st_mode)) {
        err = "not a regular file";
        ::close(own);
        return false;
    }
    if (st.st_size < 0 || static_cast<std::uintmax_t>(st.st_size) > maxBytes) {
        err = "file exceeds the size limit";
        ::close(own);
        return false;
    }
    out.fd = own;
    out.path = fileName;
    out.filename = fileName;
    out.size = static_cast<std::uintmax_t>(st.st_size);
    return true;
}

/**
 * @brief 検証済み記述子から宛先へ TOCTOU 安全にコピーする。
 * @param src コピー元
 * @param destPath 宛先パス
 * @param maxBytes 許可最大サイズ
 * @param err 失敗時の理由
 * @return 成功時 true
 */
bool copyLocalRpmTo(const LocalRpmFile &src, const std::string &destPath,
                    std::uintmax_t maxBytes, std::string &err)
{
    if (src.fd < 0) {
        err = "source file is not open";
        return false;
    }
    if (::lseek(src.fd, 0, SEEK_SET) < 0) {
        err = std::string("cannot rewind source: ") + std::strerror(errno);
        return false;
    }
    const int dst = ::open(destPath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                           S_IRUSR | S_IWUSR);
    if (dst < 0) {
        err = std::string("cannot create staging file: ") + std::strerror(errno);
        return false;
    }
    std::vector<char> buffer(1 << 20);
    std::uintmax_t total = 0;
    for (;;) {
        const ssize_t readBytes = ::read(src.fd, buffer.data(), buffer.size());
        if (readBytes < 0) {
            if (errno == EINTR)
                continue;
            err = std::string("read failed: ") + std::strerror(errno);
            ::close(dst);
            return false;
        }
        if (readBytes == 0)
            break;
        total += static_cast<std::uintmax_t>(readBytes);
        if (total > maxBytes) {
            err = "file exceeds the size limit";
            ::close(dst);
            return false;
        }
        ssize_t written = 0;
        while (written < readBytes) {
            const ssize_t part = ::write(dst, buffer.data() + written,
                                         static_cast<std::size_t>(readBytes - written));
            if (part < 0) {
                if (errno == EINTR)
                    continue;
                err = std::string("write failed: ") + std::strerror(errno);
                ::close(dst);
                return false;
            }
            written += part;
        }
    }
    if (::close(dst) != 0) {
        err = std::string("cannot finalize staging file: ") + std::strerror(errno);
        return false;
    }
    return true;
}

} // namespace qZypper
