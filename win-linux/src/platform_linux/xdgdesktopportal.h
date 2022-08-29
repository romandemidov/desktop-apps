

#ifndef XDGDESKTOPPORTAL_H
#define XDGDESKTOPPORTAL_H

#include <QWidget>
#include <QString>
#include <QPrinter>
#include "components/cprintdialog.h"

typedef QAbstractPrintDialog::PrintDialogOption PrintOption;
typedef QAbstractPrintDialog::PrintDialogOptions PrintOptions;
typedef QAbstractPrintDialog::PrintRange PrintRange;


namespace Xdg
{
typedef enum {
    OPEN = 0, SAVE = 1, FOLDER = 2
} Mode;

QStringList openXdgPortal(QWidget *parent,
                          Mode mode,
                          const QString &title,
                          const QString &file_name,
                          const QString &path,
                          const QString &filter,
                          QString *sel_filter,
                          bool sel_multiple = false);
}

class XdgPrintDialog
{
public:
    XdgPrintDialog(QPrinter *printer, QWidget *parent);
    ~XdgPrintDialog();

    void setWindowTitle(const QString &title);
    void setEnabledOptions(PrintOptions enbl_opts);
    void setOptions(PrintOptions opts);
    void setPrintRange(PrintRange print_range);
    QDialog::DialogCode exec();
    PrintRange printRange();
    PrintOptions options();
    QVector<PageRanges> getPageRanges();
    int fromPage();
    int toPage();
    uint getToken();

private:
    QPrinter *m_printer;
    QWidget *m_parent;
    QString m_title;
    PrintOptions m_options;
    PrintRange m_print_range;
    QVector<PageRanges> m_page_ranges;
    uint m_token;
};

class XdgPrintEngine
{
public:
    XdgPrintEngine(QWidget *parent, uint token, char* buf, size_t bufsize);
    ~XdgPrintEngine();

    void setWindowTitle(const QString &title);
    void startPrint();

private:
    QWidget *m_parent;
    QString  m_title;
    uint   m_token;
    char  *m_buf;
    size_t m_bufsize;
};

#endif // XDGDESKTOPPORTAL_H
