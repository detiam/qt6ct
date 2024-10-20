/*
 * Copyright (c) 2020-2024, Ilya Kotov <forkotov02@ya.ru>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <QVariant>
#include <QSettings>
#include <QGuiApplication>
#include <QScreen>
#include <QFont>
#include <QPalette>
#include <QTimer>
#include <QIcon>
#include <QRegularExpression>
#include <QMimeDatabase>
#ifdef QT_WIDGETS_LIB
#include <QStyle>
#include <QStyleFactory>
#include <QApplication>
#include <QWidget>
#if QT_CONFIG(graphicsview)
#include <QGraphicsScene>
#endif
#include <private/qapplication_p.h>
#endif
#include <QFile>
#include <QFileSystemWatcher>
#include <private/qiconloader_p.h>

#include "qt6ct.h"
#include "qt6ctplatformtheme.h"

#include <QString>
#include <QStringList>
#include <qpa/qplatformthemefactory_p.h>
#include <qpa/qwindowsysteminterface.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>

Q_LOGGING_CATEGORY(lqt6ct, "qt6ct", QtWarningMsg)

//QT_QPA_PLATFORMTHEME=qt6ct

Qt6CTPlatformTheme::Qt6CTPlatformTheme() :
    m_generalFont(*QGenericUnixTheme::font(QPlatformTheme::SystemFont)),
    m_fixedFont(*QGenericUnixTheme::font(QPlatformTheme::FixedFont))
{
    Qt6CT::initConfig();
    if(QGuiApplication::desktopSettingsAware())
    {
        readSettings();
        QMetaObject::invokeMethod(this, "applySettings", Qt::QueuedConnection);
#ifdef QT_WIDGETS_LIB
        QMetaObject::invokeMethod(this, "createFSWatcher", Qt::QueuedConnection);
#endif
    }
    qCDebug(lqt6ct) << "using qt6ct plugin";
#ifdef QT_WIDGETS_LIB
    if(!QStyleFactory::keys().contains("qt6ct-style"))
        qCCritical(lqt6ct) << "unable to find qt6ct proxy style";
#endif
}

Qt6CTPlatformTheme::~Qt6CTPlatformTheme()
{}

bool Qt6CTPlatformTheme::usePlatformNativeDialog(DialogType type) const
{
    return m_theme ? m_theme->usePlatformNativeDialog(type) :
                     QPlatformTheme::usePlatformNativeDialog(type);
}

QPlatformDialogHelper *Qt6CTPlatformTheme::createPlatformDialogHelper(DialogType type) const
{
    return m_theme ? m_theme->createPlatformDialogHelper(type) :
                     QPlatformTheme::createPlatformDialogHelper(type);
}

const QPalette *Qt6CTPlatformTheme::palette(QPlatformTheme::Palette type) const
{
    qDebug() << Q_FUNC_INFO << type;
    return m_palette ? m_palette.get() : QGenericUnixTheme::palette(type);
}

const QFont *Qt6CTPlatformTheme::font(QPlatformTheme::Font type) const
{
    if(type == QPlatformTheme::FixedFont)
        return &m_fixedFont;
    return &m_generalFont;
}

QVariant Qt6CTPlatformTheme::themeHint(QPlatformTheme::ThemeHint hint) const
{
    if(m_isIgnored)
        return QGenericUnixTheme::themeHint(hint);

    switch (hint)
    {
    case QPlatformTheme::CursorFlashTime:
        return m_cursorFlashTime;
    case MouseDoubleClickInterval:
        return m_doubleClickInterval;
    case QPlatformTheme::ToolButtonStyle:
        return m_toolButtonStyle;
    case QPlatformTheme::SystemIconThemeName:
        return m_iconTheme;
    case QPlatformTheme::StyleNames:
        qDebug() << Q_FUNC_INFO;
        return QStringList() << "qt6ct-style";
    case QPlatformTheme::IconThemeSearchPaths:
        return Qt6CT::iconPaths();
    case QPlatformTheme::DialogButtonBoxLayout:
        return m_buttonBoxLayout;
    case QPlatformTheme::KeyboardScheme:
        return m_keyboardScheme;
    case QPlatformTheme::UiEffects:
        return m_uiEffects;
    case QPlatformTheme::WheelScrollLines:
        return m_wheelScrollLines;
    case QPlatformTheme::ShowShortcutsInContextMenus:
        return m_showShortcutsInContextMenus;
    default:
        return QGenericUnixTheme::themeHint(hint);
    }
}

Qt::ColorScheme Qt6CTPlatformTheme::colorScheme() const
{
    using namespace Qt::Literals::StringLiterals;
    QDBusMessage message = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop"_L1,
        "/org/freedesktop/portal/desktop"_L1,
        "org.freedesktop.portal.Settings"_L1,
        "Read"_L1);
    message << "org.freedesktop.appearance"_L1
            << "color-scheme"_L1;

    QDBusReply<QVariant> reply = QDBusConnection::sessionBus().call(message);
    if (!reply.isValid()) {
        return Qt::ColorScheme::Unknown;
    }
    const QDBusVariant dbusVariant = qvariant_cast<QDBusVariant>(reply.value());
    enum XdgColorSchemePref {
        None,
        PreferDark,
        PreferLight
    };
    const XdgColorSchemePref xdgPref = static_cast<XdgColorSchemePref>(dbusVariant.variant().toUInt());
    switch (xdgPref) {
    case PreferDark:
        return Qt::ColorScheme::Dark;
    case PreferLight:
        return Qt::ColorScheme::Light;
    default:
        return Qt::ColorScheme::Unknown;
    }
}

QIcon Qt6CTPlatformTheme::fileIcon(const QFileInfo &fileInfo, QPlatformTheme::IconOptions iconOptions) const
{
    if((iconOptions & DontUseCustomDirectoryIcons) && fileInfo.isDir())
        return QIcon::fromTheme(QLatin1String("inode-directory"));

    QMimeDatabase db;
    QMimeType type = db.mimeTypeForFile(fileInfo);
    return QIcon::fromTheme(type.iconName());
}

void Qt6CTPlatformTheme::applySettings()
{
    if(!QGuiApplication::desktopSettingsAware() || m_isIgnored)
    {
        m_update = true;
        return;
    }

#ifdef QT_WIDGETS_LIB
    if(hasWidgets())
    {
        if(m_update)
        {
            if(FontHash *hash = qt_app_fonts_hash(); hash && hash->size())
                hash->clear();
            Qt6CT::reloadStyleInstanceSettings();
        }

        if(m_userStyleSheet != m_prevStyleSheet)
        {
            // prepend our stylesheet to that of the application
            // (first removing any previous stylesheet we have set)
            QString appStyleSheet = qApp->styleSheet();
            int prevIndex = appStyleSheet.indexOf(m_prevStyleSheet);
            if (prevIndex >= 0)
            {
                appStyleSheet.remove(prevIndex, m_prevStyleSheet.size());
                qApp->setStyleSheet(m_userStyleSheet + appStyleSheet);
            }
            else
            {
                qCDebug(lqt6ct) << "custom style sheet is disabled";
            }
            m_prevStyleSheet = m_userStyleSheet;
        }
    }
#endif

    if(m_update)
    {
        QWindowSystemInterface::handleThemeChange();
        QCoreApplication::postEvent(qGuiApp, new QEvent(QEvent::ApplicationFontChange));
    }

#ifdef QT_WIDGETS_LIB
    if(hasWidgets() && m_update)
    {
#if QT_CONFIG(graphicsview)
        for(auto scene : std::as_const(QApplicationPrivate::instance()->scene_list))
            QCoreApplication::postEvent(scene, new QEvent(QEvent::ApplicationFontChange));
#endif

        for(QWidget *w : QApplication::allWidgets())
            QCoreApplication::postEvent(w, new QEvent(QEvent::ThemeChange));
    }
#endif

    m_update = true;
}

#ifdef QT_WIDGETS_LIB
void Qt6CTPlatformTheme::createFSWatcher()
{
    QFileSystemWatcher *watcher = new QFileSystemWatcher(this);
    watcher->addPath(Qt6CT::configPath());

    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(3000);
    connect(watcher, SIGNAL(directoryChanged(QString)), timer, SLOT(start()));
    connect(timer, SIGNAL(timeout()), SLOT(updateSettings()));
}

void Qt6CTPlatformTheme::updateSettings()
{
    qCDebug(lqt6ct) << "updating settings..";
    readSettings();
    applySettings();
}
#endif

void Qt6CTPlatformTheme::readSettings()
{
    m_palette.reset();

    QSettings settings(Qt6CT::configFile(), QSettings::IniFormat);

    settings.beginGroup("Appearance");
    m_style = settings.value("style", "Fusion").toString();
    QString schemePath = settings.value("color_scheme_path").toString();
    if(!m_isIgnored && !schemePath.isEmpty() && settings.value("custom_palette", false).toBool())
    {
        schemePath = Qt6CT::resolvePath(schemePath); //replace environment variables
        m_palette = std::make_unique<QPalette>(Qt6CT::loadColorScheme(schemePath, *QPlatformTheme::palette(SystemPalette)));
    }
    m_iconTheme = settings.value("icon_theme").toString();
    //load dialogs
    if(!m_update)
    {
        //do not mix gtk2 style and gtk3 dialogs
        QStringList keys = QPlatformThemeFactory::keys();
        QString dialogs = settings.value("standard_dialogs", "default").toString();

        if(m_style.endsWith("gtk2") && dialogs == QLatin1String("gtk3"))
            dialogs = QLatin1String("gtk2");
        if(keys.contains(dialogs))
            m_theme.reset(QPlatformThemeFactory::create(dialogs));
    }

    settings.endGroup();

    settings.beginGroup("Fonts");
    m_generalFont = *QGenericUnixTheme::font(QPlatformTheme::SystemFont);
    m_generalFont.fromString(settings.value("general").toString());
    m_fixedFont = *QGenericUnixTheme::font(QPlatformTheme::FixedFont);
    m_fixedFont.fromString(settings.value("fixed").toString());
    settings.endGroup();

    settings.beginGroup("Interface");
    m_doubleClickInterval = QGenericUnixTheme::themeHint(QPlatformTheme::MouseDoubleClickInterval).toInt();
    m_doubleClickInterval = settings.value("double_click_interval", m_doubleClickInterval).toInt();
    m_cursorFlashTime = QGenericUnixTheme::themeHint(QPlatformTheme::CursorFlashTime).toInt();
    m_cursorFlashTime = settings.value("cursor_flash_time", m_cursorFlashTime).toInt();
    m_showShortcutsInContextMenus = settings.value("show_shortcuts_in_context_menus", true).toBool();
    m_buttonBoxLayout = QGenericUnixTheme::themeHint(QPlatformTheme::DialogButtonBoxLayout).toInt();
    m_buttonBoxLayout = settings.value("buttonbox_layout", m_buttonBoxLayout).toInt();
    m_keyboardScheme = QGenericUnixTheme::themeHint(QPlatformTheme::KeyboardScheme).toInt();
    m_keyboardScheme = settings.value("keyboard_scheme", m_keyboardScheme).toInt();
    QCoreApplication::setAttribute(Qt::AA_DontShowIconsInMenus, !settings.value("menus_have_icons", true).toBool());
    m_toolButtonStyle = settings.value("toolbutton_style", Qt::ToolButtonFollowStyle).toInt();
    m_wheelScrollLines = settings.value("wheel_scroll_lines", 3).toInt();

    //load effects
    m_uiEffects = QGenericUnixTheme::themeHint(QPlatformTheme::UiEffects).toInt();
    if(settings.childKeys().contains("gui_effects"))
    {
        QStringList effectList = settings.value("gui_effects").toStringList();
        m_uiEffects = 0;
        if(effectList.contains("General"))
            m_uiEffects |= QPlatformTheme::GeneralUiEffect;
        if(effectList.contains("AnimateMenu"))
            m_uiEffects |= QPlatformTheme::AnimateMenuUiEffect;
        if(effectList.contains("FadeMenu"))
            m_uiEffects |= QPlatformTheme::FadeMenuUiEffect;
        if(effectList.contains("AnimateCombo"))
            m_uiEffects |= QPlatformTheme::AnimateComboUiEffect;
        if(effectList.contains("AnimateTooltip"))
            m_uiEffects |= QPlatformTheme::AnimateTooltipUiEffect;
        if(effectList.contains("FadeTooltip"))
            m_uiEffects |= QPlatformTheme::FadeTooltipUiEffect;
        if(effectList.contains("AnimateToolBox"))
            m_uiEffects |= QPlatformTheme::AnimateToolBoxUiEffect;
    }

    //load style sheets
#ifdef QT_WIDGETS_LIB
    QStringList qssPaths = settings.value("stylesheets").toStringList();
    m_userStyleSheet = loadStyleSheets(qssPaths);
#endif
    settings.endGroup();

    //load troubleshooting
    if(!m_update)
    {
        settings.beginGroup("Troubleshooting");
        m_isIgnored = settings.value("ignored_applications").toStringList().contains(QCoreApplication::applicationFilePath());
        int forceRasterWidgets = settings.value("force_raster_widgets", Qt::PartiallyChecked).toInt();
        if(!m_isIgnored && forceRasterWidgets == Qt::Checked)
            QCoreApplication::setAttribute(Qt::AA_ForceRasterWidgets, true);
        else if(!m_isIgnored && forceRasterWidgets == Qt::Unchecked)
            QCoreApplication::setAttribute(Qt::AA_ForceRasterWidgets, false);
        settings.endGroup();
    }
}

#ifdef QT_WIDGETS_LIB
bool Qt6CTPlatformTheme::hasWidgets()
{
    return qobject_cast<QApplication *> (qApp) != nullptr;
}
#endif

QString Qt6CTPlatformTheme::loadStyleSheets(const QStringList &paths)
{
    QString content;
    for(const QString &path : qAsConst(paths))
    {
        if(!QFile::exists(path))
            continue;

        QFile file(path);
        file.open(QIODevice::ReadOnly);
        content.append(QString::fromUtf8(file.readAll()));
        if(!content.endsWith(QChar::LineFeed))
            content.append(QChar::LineFeed);
    }
    static const QRegularExpression regExp("//.*\n");
    content.replace(regExp, "\n");
    return content;
}
