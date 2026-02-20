# KI18n (Internationalization)

> Source: `/usr/include/KF6/KI18n/` headers

## Setup

### Application Domain

```cpp
#include <KLocalizedString>

// Must be called early in main()
KLocalizedString::setApplicationDomain("krema");
```

### Available Translations

```cpp
QSet<QString> langs = KLocalizedString::availableApplicationTranslations();
QStringList currentLangs = KLocalizedString::languages();

// Override languages
KLocalizedString::setLanguages({"ko", "en"});
KLocalizedString::clearLanguages();  // Reset to system locale
```

---

## i18n Macros

### Basic Translation

```cpp
// Simple string
QString msg = i18n("Hello, World!");

// With arguments (positional %1, %2, ...)
QString msg = i18n("File %1 saved at %2", fileName, time);
```

### With Context (disambiguation)

```cpp
// context = disambiguation hint for translators
QString msg = i18nc("menu item", "Open");
QString msg = i18nc("dialog title", "Open");
```

### With Plural

```cpp
// Plural forms (number is first argument)
QString msg = i18np("1 file", "%1 files", count);
```

### Context + Plural

```cpp
QString msg = i18ncp("file count in dock", "1 file", "%1 files", count);
```

### With Explicit Domain

```cpp
// For libraries that use a different translation domain
QString msg = i18nd("libkdock", "Hello");
QString msg = i18ndc("libkdock", "context", "Hello");
QString msg = i18ndp("libkdock", "1 item", "%1 items", count);
QString msg = i18ndcp("libkdock", "context", "1 item", "%1 items", count);
```

---

## KUIT Markup-Aware Macros

Use `xi18n` variants for rich text with semantic markup:

```cpp
QString msg = xi18n("Press <shortcut>Ctrl+S</shortcut> to save.");
QString msg = xi18nc("context", "Use <command>krema</command>.");
```

KUIT tags: `<filename>`, `<command>`, `<shortcut>`, `<emphasis>`, `<link>`, `<email>`, `<resource>`, `<nl>`, `<para>`, `<title>`, `<subtitle>`, `<list>`, `<item>`, etc.

---

## KLocalizedString Methods

### Argument Substitution

```cpp
KLocalizedString ks = ki18n("Value: %1 of %2");

// Type-safe substitution
ks = ks.subs(42);                      // int
ks = ks.subs(3.14, 0, 'f', 2);        // double with format
ks = ks.subs(QString("hello"));        // string
ks = ks.subs(QChar('x'));              // char
ks = ks.subs(otherKLocalizedString);   // nested localization

// Field width and fill character
ks = ks.subs(42, 5);                   // right-aligned, width 5
ks = ks.subs(42, -5);                  // left-aligned, width 5
ks = ks.subs(42, 5, 10, QLatin1Char('0')); // zero-padded

QString result = ks.toString();
```

### Modifiers

```cpp
ki18n("text")
    .withLanguages({"ko", "ja"})        // Override languages
    .withDomain("other-domain")          // Override domain
    .withFormat(Kuit::PlainText)         // Force plain text output
    .inContext("category", "science")    // Dynamic context
    .relaxSubs()                         // Relax placeholder matching
    .ignoreMarkup()                      // Disable KUIT resolution
    .toString();
```

### Utility

```cpp
QByteArray original = ks.untranslatedText();
bool empty = ks.isEmpty();

// Remove accelerator markers (&File -> File)
QString clean = KLocalizedString::removeAcceleratorMarker("&File");

// Find localized file (e.g., help files)
QString path = KLocalizedString::localizedFilePath("/path/to/file.html");

// Custom locale directory
KLocalizedString::addDomainLocaleDir("krema", "/custom/locale/path");
```

---

## QML Integration

### KLocalizedContext (deprecated in 6.8)

```cpp
// In C++ setup
#include <KLocalizedContext>

QQmlEngine engine;
engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
```

```qml
// In QML
text: i18n("Hello, %1", userName)
text: i18nc("greeting context", "Hello")
text: i18np("1 item", "%1 items", count)
```

### KLocalizedQmlContext (KF6.8+, replacement)

Use `KLocalization::setupLocalizedContext(engine)` instead.

---

## Qt uic Integration

For `.ui` files compiled with `uic -tr tr2i18n`:

```cpp
tr2i18n("text", "comment")     // Standard
tr2i18nd("domain", "text", "comment")  // With domain
```

---

## Spin Box Formatting

```cpp
#include <KLocalization>

QSpinBox spinBox;
KLocalization::setupSpinBoxFormatString(&spinBox, ki18nc("@item", "%v items"));
// %v = number placeholder, handles plural/RTL correctly

// Update on language change
KLocalization::retranslateSpinBoxFormatString(&spinBox);
```

---

## Macro Summary

| Macro | Context | Plural | Domain | Markup |
|-------|---------|--------|--------|--------|
| `i18n` | - | - | - | - |
| `i18nc` | Yes | - | - | - |
| `i18np` | - | Yes | - | - |
| `i18ncp` | Yes | Yes | - | - |
| `i18nd` | - | - | Yes | - |
| `i18ndc` | Yes | - | Yes | - |
| `i18ndp` | - | Yes | Yes | - |
| `i18ndcp` | Yes | Yes | Yes | - |
| `xi18n` | - | - | - | Yes |
| `xi18nc` | Yes | - | - | Yes |
| `xi18np` | - | Yes | - | Yes |
| `xi18ncp` | Yes | Yes | - | Yes |

Prefix `k` (e.g., `ki18n`) returns `KLocalizedString` instead of `QString` — for deferred substitution.
