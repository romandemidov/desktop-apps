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

#ifndef CMAINWINDOW_H
#define CMAINWINDOW_H

#include "cmainwindowbase.h"
#include "cmainpanelimpl.h"
#include <QtWidgets/QApplication>
#include <QMainWindow>
#include <QShowEvent>
#include <QCloseEvent>
#include <QMargins>
#include <QRect>


class CMainWindow : public CMainWindowBase, public QMainWindow
{
public:
    explicit CMainWindow(const QRect&);
    explicit CMainWindow(const QRect&, const QString&, QWidget*);
    explicit CMainWindow(const QRect&, const QString&, QCefView*);
    virtual ~CMainWindow();

    HWND handle() const; //
    void show(bool); //
    void hide(); //
    void toggleBorderless(bool); //
    void setWindowState(Qt::WindowState); //
    void setWindowBackgroundColor(const QColor&); //
    void setWindowColors(const QColor& background, const QColor& border); //
    bool isVisible(); //

    virtual CMainPanel * mainPanel() const final; //
    virtual QRect windowRect() const final; //
    virtual void updateScaling() final; //
    virtual void bringToTop() final; //
    virtual void setWindowTitle(const QString&) final; //
    virtual void adjustGeometry() final; //

    virtual bool holdView(int id) const override;   //
    virtual void applyTheme(const std::wstring&) override;  //

#if (QT_VERSION < QT_VERSION_CHECK(5, 10, 0))
    // because of QTBUG-67211
    virtual bool pointInTabs(const QPoint& pt) const override;
#endif

#ifdef _UPDMODULE
    static void checkUpdates();
    static void setAutocheckUpdatesInterval(const QString&);
#endif

protected:
    void captureMouse(); //

    virtual void setScreenScalingFactor(double f) override; //
    virtual void onMinimizeEvent() override; //
    virtual void onMaximizeEvent() override; //
    virtual void onExitSizeMove() override; //
    virtual void onCloseEvent() override; //

private:
    explicit CMainWindow(const QRect&, const WindowType, const QString&, QWidget*);
    friend auto refresh_window_scaling_factor(CMainWindow * window) -> void;
#ifdef _UPDMODULE
    static void updateFound();
    static void updateNotFound();
    static void updateError();
#endif
    WindowBase::CWindowGeometry const& minimumSize() const; //
    WindowBase::CWindowGeometry const& maximumSize() const; //
    void doClose(); //
    void applyWindowState(Qt::WindowState); //
    void slot_windowChangeState(Qt::WindowState); //
    void slot_windowClose(); //
    void slot_mainPageReady(); //
    void slot_modalDialog(bool status, HWND h); //
    void toggleResizeable(); //
    void setResizeable(bool resizeable); //
    void setResizeableAreaWidth(int width); //
    void setContentsMargins(int left, int top, int right, int bottom); //
    void setMinimumSize(const int width, const int height); //
    void setMaximumSize(const int width, const int height); //
    void removeMinimumSize(); //
    void removeMaximumSize(); //
    void activateWindow(); //

    bool isResizeable() {return m_isResizeable;} //
    bool isSetMinimumSize(); //
    bool isSetMaximumSize(); //

    int getMinimumHeight() const; //
    int getMinimumWidth() const; //
    int getMaximumHeight(); //
    int getMaximumWidth(); //

    virtual void showEvent(QShowEvent *event) final;    //
    virtual void closeEvent(QCloseEvent *event) final;  //
    virtual void changeEvent(QEvent *event) final;  //
    virtual bool nativeEvent(const QByteArray &eventType, void *message, long *result) final; //
    virtual void captureMouse(int tabindex) final;  //

    WindowType m_winType;
    WindowBase::CWindowGeometry m_minSize;
    WindowBase::CWindowGeometry m_maxSize;
    QMetaObject::Connection m_modalSlotConnection;
    CMainPanelImpl *_m_pMainPanel;
    Qt::WindowStates m_previousState;

    QRect m_moveNormalRect;
    QRect m_window_rect;
    QMargins m_margins;
    QMargins m_frame;

    HWND m_hWnd;
    HWND m_modalHwnd;

    int  m_borderWidth;
    bool m_borderless;
    bool m_visible;
    bool m_closed;
    bool m_skipSizing;
    bool m_isMaximized;
    bool m_isResizeable;
    bool m_taskBarClicked;
    bool m_windowActivated;
};

#endif
