#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QObject>
#include <QPalette>
#include <QString>

#include <optional>

#include "Core/Theme/Fonts.hpp"
#include "Core/Theme/Tokens.hpp"

class QJsonObject;

namespace Acheron {
namespace Core {
namespace Theme {

class Manager : public QObject
{
    Q_OBJECT
public:
    static Manager &instance();

    QColor color(Token token) const;
    QFont font(FontRole role) const;

    bool hasOverride(Token token) const;
    void setOverride(Token token, const QColor &color);
    void clearOverride(Token token);
    void resetAll();
    void setOverrides(const QHash<Token, QColor> &overrides);

    bool usesSystemColors() const;
    void setUseSystemColors(bool enabled);
    bool isSystemControlled(Token token) const;

    static bool systemStyleEnabled();
    static void setSystemStyleEnabled(bool enabled);
    static bool startsWithSystemStyle();

    bool hasFontOverride(FontRole role) const;
    void setFontOverride(FontRole role, const QFont &font);
    void clearFontOverride(FontRole role);

    QPalette buildPalette() const;
    void apply();
    void applyFonts();

    // load/save only does overrides, export does everything as resolved
    bool load();
    bool save() const;
    bool exportTo(const QString &path) const;
    bool importFrom(const QString &path);

signals:
    void themeChanged();
    void metricsChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Manager() = default;
    Q_DISABLE_COPY(Manager)

    static QString defaultThemePath();
    QJsonObject toObject(bool includeDefaults) const;
    void loadFromObject(const QJsonObject &obj);
    QColor customColor(Token token) const;
    std::optional<QColor> systemColor(Token token) const;
    void restyle();

    QHash<Token, QColor> overrides;
    QHash<FontRole, QFont> fontOverrides;

    bool systemColors = false;
    bool watchingApplication = false;
    bool applying = false;
    QPalette styledPalette;
};

} // namespace Theme
} // namespace Core
} // namespace Acheron
