#include <QMap>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QCryptographicHash>
#include <Windows.h>

#define VERSION_FILE "/.version_control.lst"
#define MAGIC_NUMBER 0x836A05F0

using std::wstring;


void showMessage(const QString& str)
{
    wstring lpText = str.toStdWString();
    MessageBoxW(NULL, lpText.c_str(), TEXT("Version List Generator"), MB_ICONWARNING | MB_SERVICE_NOTIFICATION_NT3X | MB_SETFOREGROUND);
}

void getFileHash(const QString &fileName, QString &hash_str)
{
    QFile file(fileName);
    if (file.open(QFile::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Md5);
        if (hash.addData(&file))
            hash_str = QString(hash.result().toHex());
        file.close();
    }
}

//QString getParentPath(const QString &path)
//{
//    int delim = path.lastIndexOf('\\');
//    return (delim == -1) ? path : path.mid(0, delim);
//}

//uint mapCapacity(const QMap<QString,QString> &map){
//    uint cap = sizeof(map);
//    for (QMap<QString,QString>::const_iterator it = map.begin(); it != map.end(); ++it) {
//        cap += it.key().capacity();
//        cap += it.value().capacity();
//    }
//    return cap;
//}

int main(int argc, char *argv[])
{
    if (argc > 1) {
        const QString path = QString::fromUtf8(argv[1]);
        if (!QFileInfo(path).isDir()) {
            showMessage("Path specified incorrectly!");
            return 1;
        }

        if (QFile::exists(path + VERSION_FILE) && !QFile::remove(path + VERSION_FILE)) {
            showMessage("Can't remove old file: " + path + VERSION_FILE);
            return 1;
        }

        QMap<QString, QString> verMap;
        QStringList filters{"*.*"};
        QDirIterator it(path, filters, QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString fullPath = it.next();
            QString hash("0");
            getFileHash(fullPath, hash);
            const QString subPath = fullPath.mid(path.length());
            verMap.insert(subPath, hash);
        }

        QFile verFile(path + VERSION_FILE);
        if (!verFile.open(QFile::WriteOnly)) {
            showMessage("Can't create file: " + path + VERSION_FILE);
            return 1;
        }
        QDataStream out(&verFile);
        out.setVersion(QDataStream::Qt_5_9);
        out << MAGIC_NUMBER;
        out << verMap;
        out << verMap.size();
        verFile.close();

    } else {
        showMessage("Invalid arguments when initializing!");
        return 1;
    }
    showMessage("Generation completed!");

    return 0;
}
