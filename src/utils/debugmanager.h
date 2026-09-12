// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QLoggingCategory>
#include <QObject>

namespace krema
{

class DebugManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool appEnabled READ appEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool geomEnabled READ geomEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool inputEnabled READ inputEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool animEnabled READ animEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool previewEnabled READ previewEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool modelEnabled READ modelEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool shellEnabled READ shellEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool shaderEnabled READ shaderEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool configEnabled READ configEnabled NOTIFY enabledChanged)
public:
    static DebugManager *self();

    enum Category {
        App,
        Geom,
        Input,
        Anim,
        Preview,
        Model,
        Shell,
        Shader,
        Config,
        Count
    };
    Q_ENUM(Category)

    bool isEnabled(Category cat) const
    {
        return m_enabled[cat];
    }
    void setEnabled(Category cat, bool enabled)
    {
        if (m_enabled[cat] != enabled) {
            m_enabled[cat] = enabled;
            Q_EMIT enabledChanged();
        }
    }

    bool appEnabled() const
    {
        return m_enabled[App];
    }
    bool geomEnabled() const
    {
        return m_enabled[Geom];
    }
    bool inputEnabled() const
    {
        return m_enabled[Input];
    }
    bool animEnabled() const
    {
        return m_enabled[Anim];
    }
    bool previewEnabled() const
    {
        return m_enabled[Preview];
    }
    bool modelEnabled() const
    {
        return m_enabled[Model];
    }
    bool shellEnabled() const
    {
        return m_enabled[Shell];
    }
    bool shaderEnabled() const
    {
        return m_enabled[Shader];
    }
    bool configEnabled() const
    {
        return m_enabled[Config];
    }

    static const char *categoryName(Category cat);
    static const char *categoryColor(Category cat);

    // QML-accessible logging methods
    Q_INVOKABLE void app(const QString &msg);
    Q_INVOKABLE void geom(const QString &msg);
    Q_INVOKABLE void input(const QString &msg);
    Q_INVOKABLE void anim(const QString &msg);
    Q_INVOKABLE void preview(const QString &msg);
    Q_INVOKABLE void model(const QString &msg);
    Q_INVOKABLE void shell(const QString &msg);
    Q_INVOKABLE void shader(const QString &msg);
    Q_INVOKABLE void config(const QString &msg);

Q_SIGNALS:
    void enabledChanged();

private:
    explicit DebugManager(QObject *parent = nullptr);
    bool m_enabled[Count];
};

} // namespace krema

Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcGeom)
Q_DECLARE_LOGGING_CATEGORY(lcInput)
Q_DECLARE_LOGGING_CATEGORY(lcAnim)
Q_DECLARE_LOGGING_CATEGORY(lcPreview)
Q_DECLARE_LOGGING_CATEGORY(lcModel)
Q_DECLARE_LOGGING_CATEGORY(lcShell)
Q_DECLARE_LOGGING_CATEGORY(lcShader)
Q_DECLARE_LOGGING_CATEGORY(lcConfig)
