# KAboutData (Application Metadata)

> Source: `/usr/include/KF6/KCoreAddons/kaboutdata.h` headers

## Overview

KAboutData stores application metadata (name, version, license, authors). Used by:
- D-Bus service registration (via `organizationDomain`)
- KGlobalAccel component identification
- About dialogs
- Command-line `--version` / `--author` options
- KCrash crash handler

---

## Basic Usage

```cpp
#include <KAboutData>

KAboutData aboutData(
    "krema",                              // componentName (internal ID)
    i18n("Krema"),                        // displayName
    "0.1.0",                              // version
    i18n("A dock for KDE Plasma 6"),      // shortDescription
    KAboutLicense::GPL_V3,                // license
    i18n("(C) 2024-2026"),                // copyrightStatement
    QString(),                            // otherText
    "https://github.com/user/krema"       // homepage
);

aboutData.addAuthor(i18n("Author Name"), i18n("Developer"), "email@example.com");

// Set as application data (makes it globally accessible)
KAboutData::setApplicationData(aboutData);
```

---

## KAboutData Methods

### Constructor

```cpp
KAboutData(const QString &componentName,          // Internal ID (used for D-Bus, config)
           const QString &displayName,             // Human-readable name
           const QString &version,                 // Version string
           const QString &shortDescription,        // One-line description
           KAboutLicense::LicenseKey licenseType,  // License enum
           const QString &copyrightStatement = {},
           const QString &otherText = {},
           const QString &homePageAddress = {},
           const QString &bugAddress = "submit@bugs.kde.org");
```

### Static Methods

```cpp
static void setApplicationData(const KAboutData &aboutData);  // Set globally
static KAboutData applicationData();                            // Retrieve globally
```

### Authors & Credits (Chainable)

```cpp
KAboutData &addAuthor(const QString &name, const QString &task = {},
                      const QString &email = {}, const QString &web = {},
                      const QUrl &avatarUrl = {});

KAboutData &addCredit(const QString &name, const QString &task = {},
                      const QString &email = {}, const QString &web = {});

KAboutData &setTranslator(const QString &name, const QString &email);
```

### Metadata Setters (Chainable)

```cpp
KAboutData &setComponentName(const QString &name);
KAboutData &setDisplayName(const QString &name);
KAboutData &setVersion(const QByteArray &version);
KAboutData &setShortDescription(const QString &desc);
KAboutData &setHomepage(const QString &url);
KAboutData &setBugAddress(const QByteArray &address);
KAboutData &setCopyrightStatement(const QString &text);
KAboutData &setOtherText(const QString &text);
KAboutData &setProgramLogo(const QVariant &image);  // QIcon/QPixmap in QVariant
KAboutData &setOrganizationDomain(const QByteArray &domain);
KAboutData &setDesktopFileName(const QString &name);
```

### Metadata Getters

```cpp
QString componentName() const;     // Internal ID
QString displayName() const;       // Human-readable name
QString version() const;
QString shortDescription() const;
QString homepage() const;
QString bugAddress() const;
QString copyrightStatement() const;
QString organizationDomain() const;
QString desktopFileName() const;
QVariant programLogo() const;
QList<KAboutPerson> authors() const;
QList<KAboutPerson> credits() const;
QList<KAboutLicense> licenses() const;
```

### License Management

```cpp
KAboutData &setLicense(KAboutLicense::LicenseKey key);
KAboutData &setLicense(KAboutLicense::LicenseKey key, KAboutLicense::VersionRestriction restriction);
KAboutData &addLicense(KAboutLicense::LicenseKey key);  // Multiple licenses
KAboutData &setLicenseText(const QString &text);         // Custom license text
KAboutData &setLicenseTextFile(const QString &file);     // From file
```

### Command-Line Integration

```cpp
bool setupCommandLine(QCommandLineParser *parser);   // Add --version, --author, --license
void processCommandLine(QCommandLineParser *parser);  // Handle those options
```

### Components (Third-Party Libraries)

```cpp
KAboutData &addComponent(const QString &name, const QString &desc = {},
                          const QString &version = {}, const QString &web = {},
                          KAboutLicense::LicenseKey license = KAboutLicense::Unknown);
```

---

## KAboutLicense

### LicenseKey Enum

```cpp
enum LicenseKey {
    Custom = -2,
    File = -1,
    Unknown = 0,
    GPL = 1,         // GPL v2 (alias: GPL_V2)
    LGPL = 2,        // LGPL v2 (alias: LGPL_V2)
    BSD_2_Clause = 3,
    Artistic = 4,
    GPL_V3 = 5,
    LGPL_V3 = 6,
    LGPL_V2_1 = 7,
    MIT = 8,
    ODbL_V1 = 9,      // since 6.9
    Apache_V2 = 10,    // since 6.9
    FTL = 11,          // since 6.9
    BSL_V1 = 12,       // since 6.9
    BSD_3_Clause = 13,  // since 6.9
    CC0_V1 = 14,       // since 6.9
    MPL_V2 = 15,       // since 6.11
};
```

### VersionRestriction

```cpp
enum VersionRestriction {
    OnlyThisVersion,  // "GPL v3 only"
    OrLaterVersions,  // "GPL v3 or later" (default)
};
```

### Methods

```cpp
QString text() const;                              // Full license text
QString name(NameFormat format = ShortName) const;  // "GPL" or full name
LicenseKey key() const;
QString spdx() const;                             // "GPL-3.0-or-later"
static KAboutLicense byKeyword(const QString &keyword);
```

---

## KAboutPerson

```cpp
class KAboutPerson {
    QString name() const;
    QString task() const;
    QString emailAddress() const;
    QString webAddress() const;
    QUrl avatarUrl() const;

    static KAboutPerson fromJSON(const QJsonObject &obj);
};
```

---

## Organization Domain

The `organizationDomain` is derived from `homePageAddress` if not explicitly set:
- `https://www.kde.org` → `kde.org` (strips leading subdomain if 3+ parts)
- `https://kde.org` → `kde.org`

Used for:
- **D-Bus service name:** `org.kde.<componentName>`
- **Desktop file name:** `org.kde.<componentName>.desktop`

---

## Integration Points

| Framework | How KAboutData is Used |
|-----------|----------------------|
| **KGlobalAccel** | `componentName()` identifies the shortcut component |
| **KDBusService** | `organizationDomain()` + `componentName()` for D-Bus name |
| **KCrash** | `internalProductName()`, `internalVersion()` for crash reports |
| **QCommandLineParser** | `--version`, `--author`, `--license` options |
| **KAboutApplicationDialog** | All metadata for about dialog |

---

## Krema Setup

```cpp
void Application::setupAboutData() {
    KAboutData aboutData(
        "krema",
        i18n("Krema"),
        "0.1.0",
        i18n("A dock for KDE Plasma 6"),
        KAboutLicense::GPL_V3
    );

    KAboutData::setApplicationData(aboutData);
}
```
