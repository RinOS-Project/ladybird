/*
 * Copyright (c) 2022, Filiph Sandström <filiph.sandstrom@filfatstudios.com>
 * Copyright (c) 2023, Cameron Youell <cameronyouell@gmail.com>
 * Copyright (c) 2024, Jamie Mansfield <jmansfield@cadixdev.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <UI/Qt/Settings.h>
#include <UI/Qt/StringUtils.h>

#include <QCryptographicHash>

namespace Ladybird {

Settings::Settings()
{
    m_qsettings = make<QSettings>(QSettings::IniFormat, QSettings::UserScope, "Ladybird", "Ladybird", this);
}

ByteString Settings::directory()
{
    return LexicalPath::dirname(ak_byte_string_from_qstring(m_qsettings->fileName()));
}

Optional<QPoint> Settings::last_position()
{
    if (m_qsettings->contains("last_position"))
        return m_qsettings->value("last_position", QPoint()).toPoint();
    return {};
}

void Settings::set_last_position(QPoint const& last_position)
{
    m_qsettings->setValue("last_position", last_position);
}

QSize Settings::last_size()
{
    return m_qsettings->value("last_size", QSize(800, 600)).toSize();
}

void Settings::set_last_size(QSize const& last_size)
{
    m_qsettings->setValue("last_size", last_size);
}

bool Settings::is_maximized()
{
    return m_qsettings->value("is_maximized", QVariant(false)).toBool();
}

void Settings::set_is_maximized(bool is_maximized)
{
    m_qsettings->setValue("is_maximized", is_maximized);
}

bool Settings::show_menubar()
{
    return m_qsettings->value("show_menubar", false).toBool();
}

void Settings::set_show_menubar(bool show_menubar)
{
    m_qsettings->setValue("show_menubar", show_menubar);
    emit show_menubar_changed(show_menubar);
}

Optional<bool> Settings::geolocation_permission(QString const& origin) const
{
    auto key_hash = QCryptographicHash::hash(origin.toUtf8(), QCryptographicHash::Sha256).toHex();
    auto key = QStringLiteral("site_permissions/geolocation/") + QString::fromLatin1(key_hash);
    auto value = m_qsettings->value(key);
    if (!value.isValid())
        return {};
    if (value == QStringLiteral("allow"))
        return true;
    if (value == QStringLiteral("deny"))
        return false;
    return {};
}

bool Settings::set_geolocation_permission(QString const& origin, bool allowed)
{
    auto key_hash = QCryptographicHash::hash(origin.toUtf8(), QCryptographicHash::Sha256).toHex();
    auto key = QStringLiteral("site_permissions/geolocation/") + QString::fromLatin1(key_hash);
    auto value = allowed ? QStringLiteral("allow") : QStringLiteral("deny");
    m_qsettings->setValue(key, value);
    m_qsettings->sync();
    return m_qsettings->status() == QSettings::NoError && m_qsettings->value(key).toString() == value;
}

}
