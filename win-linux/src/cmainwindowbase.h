/*
 * (c) Copyright Ascensio System SIA 2010-2019
 *
 * This program is a free software product. You can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License (AGPL)
 * version 3 as published by the Free Software Foundation. In accordance with
 * Section 7(a) of the GNU AGPL its Section 15 shall be amended to the effect
 * that Ascensio System SIA expressly excludes the warranty of non-infringement
 * of any third-party rights.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR  PURPOSE. For
 * details, see the GNU AGPL at: http://www.gnu.org/licenses/agpl-3.0.html
 *
 * You can contact Ascensio System SIA at 20A-12 Ernesta Birznieka-Upisha
 * street, Riga, Latvia, EU, LV-1050.
 *
 * The  interactive user interfaces in modified source and object code versions
 * of the Program must display Appropriate Legal Notices, as required under
 * Section 5 of the GNU AGPL version 3.
 *
 * Pursuant to Section 7(b) of the License you must retain the original Product
 * logo when distributing the program. Pursuant to Section 7(e) we decline to
 * grant you any rights under trademark law for use of our trademarks.
 *
 * All the Product's GUI elements, including illustrations and icon sets, as
 * well as technical writing content are licensed under the terms of the
 * Creative Commons Attribution-ShareAlike 4.0 International. See the License
 * terms at http://creativecommons.org/licenses/by-sa/4.0/legalcode
 *
*/

#ifndef CMAINWINDOWBASE_H
#define CMAINWINDOWBASE_H

#define WINDOW_MIN_WIDTH    500
#define WINDOW_MIN_HEIGHT   300

#define MAIN_WINDOW_MIN_WIDTH  960
#define MAIN_WINDOW_MIN_HEIGHT 661
#define MAIN_WINDOW_DEFAULT_SIZE QSize(1324,800)

#define BUTTON_MAIN_WIDTH   112
#define MAIN_WINDOW_BORDER_WIDTH 4
#define WINDOW_TITLE_MIN_WIDTH 200
#define TOOLBTN_HEIGHT      28
#define TOOLBTN_WIDTH       40
#define TITLE_HEIGHT        28

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <memory>
#include "cmainpanel.h"

#ifdef _WIN32
# include <windows.h>
# include <windowsx.h>
# include <dwmapi.h>

namespace WindowBase
{
    enum class Style : DWORD
    {
        windowed = ( WS_OVERLAPPED | WS_THICKFRAME | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN ),
        aero_borderless = ( WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME | WS_CLIPCHILDREN )
    };
    struct CWindowGeometry
    {
        CWindowGeometry() {}
        bool required = false;
        int width = 0;
        int height = 0;
    };
}
#endif

enum class WindowType : uint_fast8_t
{
    MAIN, SINGLE, REPORTER
};

class CElipsisLabel : public QLabel
{
public:
    CElipsisLabel(const QString &text, QWidget *parent = Q_NULLPTR);
    CElipsisLabel(QWidget *parent = Q_NULLPTR, Qt::WindowFlags f = Qt::WindowFlags());

    auto setText(const QString&) -> void;
    auto setEllipsisMode(Qt::TextElideMode) -> void;
    auto updateText() -> void;

protected:
    void resizeEvent(QResizeEvent *event) override;
    using QLabel::setText;

private:
    QString orig_text;
    Qt::TextElideMode elide_mode = Qt::ElideRight;
};

class CMainWindowBase
{
public:
    CMainWindowBase(QRect& rect);
    virtual ~CMainWindowBase();

    virtual QWidget * editor(int index);
    virtual QString documentName(int vid);
    virtual const QObject * receiver() {return nullptr;}
    virtual CMainPanel * mainPanel() const = 0;
    virtual QRect windowRect() const {return QRect();}

    virtual void setScreenScalingFactor(double);
    virtual void setWindowTitle(const QString&);
    virtual void adjustGeometry() {};
    virtual void bringToTop() {}
    virtual void focus() {}
    virtual void updateScaling();    
    virtual void bringToTop() const {}
    virtual void selectView(int id) const;
    virtual void selectView(const QString& url) const;
    virtual void applyTheme(const std::wstring&);
    virtual void captureMouse(int tab_index);

    virtual double scaling() const;
    virtual int attachEditor(QWidget *, int index = -1);
    virtual int attachEditor(QWidget *, const QPoint&);
    virtual int editorsCount() const;
    virtual int editorsCount(const std::wstring& portal) const;
    virtual bool isCustomWindowStyle();
    virtual bool pointInTabs(const QPoint& pt) const;
    virtual bool holdView(int id) const;

protected:
    virtual QWidget * createTopPanel(QWidget *, const QString&);
    virtual QWidget * createMainPanel(QWidget *, const QString&, bool custom = true, QWidget * view = nullptr);
    virtual QPushButton * createToolButton(QWidget * parent = nullptr, const QString& name = QString(""));
    virtual void onCloseEvent() {};
    virtual void onMinimizeEvent() {};
    virtual void onMaximizeEvent() {};
    virtual void onSizeEvent(int);
    virtual void onMoveEvent(const QRect&) {};
    virtual void onExitSizeMove() {};
    virtual void onDpiChanged(double newfactor, double prevfactor) {};
    virtual void updateTitleCaption();
    virtual void focusMainPanel();
    virtual int calcTitleCaptionWidth();
    inline int dpiCorrectValue(int v) const
    {
        return int(v * m_dpiRatio);
    }

    QWidget * m_boxTitleBtns;
    QWidget * m_pMainPanel;
    QWidget * m_pMainView;
    QPushButton * m_buttonMinimize;
    QPushButton * m_buttonMaximize;
    QPushButton * m_buttonClose;
    CElipsisLabel * m_labelTitle;
    double m_dpiRatio;

    QGridLayout *m_pCentralLayout;
    QWidget *m_pCentralWidget;

private:
    class impl;
    std::unique_ptr<impl> pimpl;
    void initTopButtons(QWidget *parent);
};

#endif // CMAINWINDOWBASE_H
