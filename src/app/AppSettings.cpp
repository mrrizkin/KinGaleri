#include "app/AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleHints>

namespace kg {

namespace {

// Keys are grouped to match the settings page so a hand-edited config file is
// still readable. Never renamed once shipped: that would silently reset choices.
//
// NOTE: no group is named "general". QSettings maps the `[General]` INI section
// onto its root group, so `general/<key>` would be read back as the ungrouped
// `<key>` and the default would always win.
constexpr auto kStartupPageKey = "startup/page";
constexpr auto kLastPageKey = "startup/last_page";
constexpr auto kConfirmDestructiveKey = "safety/confirm_destructive";
constexpr auto kDeveloperModeKey = "developer/mode";
// Kept under "appearance" from M4 so existing installs keep their choice.
constexpr auto kThemeKey = "appearance/theme";
constexpr auto kLanguageKey = "appearance/language";
constexpr auto kThumbnailSizeKey = "appearance/thumbnail_size";
constexpr auto kAccentKey = "interface/accent";
constexpr auto kSystemIconsKey = "interface/system_icons";
constexpr auto kGridSpacingKey = "interface/grid_spacing";
constexpr auto kShowSidebarKey = "interface/show_sidebar";
constexpr auto kSidebarWidthKey = "interface/sidebar_width";
constexpr auto kShowDateHeadersKey = "interface/show_date_headers";
constexpr auto kAnimateTransitionsKey = "interface/animate_transitions";
constexpr auto kHoverEffectsKey = "interface/hover_effects";
constexpr auto kShowPerfOverlayKey = "interface/show_perf_overlay";
constexpr auto kAutoWatchKey = "library/auto_watch";
constexpr auto kRescanOnStartupKey = "library/rescan_on_startup";
constexpr auto kFollowSymlinksKey = "library/follow_symlinks";
constexpr auto kIncludeHiddenKey = "library/include_hidden";
constexpr auto kIgnoreJunkKey = "library/ignore_junk";
constexpr auto kJunkPatternsKey = "library/junk_patterns";
constexpr auto kIgnoredFoldersKey = "library/ignored_folders";
constexpr auto kIgnoredPatternsKey = "library/ignored_patterns";
constexpr auto kThumbnailMemoryKey = "thumbs/memory_mb";
constexpr auto kVideoAutoplayKey = "playback/autoplay";
constexpr auto kVideoLoopKey = "playback/loop";
constexpr auto kVideoMuteKey = "playback/mute";
constexpr auto kVideoVolumeKey = "playback/volume";

constexpr int kMinThumbnailSize = 80;
constexpr int kMaxThumbnailSize = 384;
constexpr int kThumbnailStep = 8;
constexpr int kDefaultThumbnailSize = 168;

constexpr int kMinThumbnailMemoryMb = 32;
constexpr int kMaxThumbnailMemoryMb = 2048;

constexpr int kMinSidebarWidth = 160;
constexpr int kMaxSidebarWidth = 360;
constexpr int kMaxGridSpacing = 24;

/// Locale name used in the .qm file stem. English is the source language, so it
/// has no translation to load. "system" follows the OS locale's language subtag
/// instead of always resolving to English.
QString localeStem(const QString &language)
{
    if (language == QLatin1String("id")) {
        return QStringLiteral("id");
    }
    if (language == QLatin1String("en")) {
        return QStringLiteral("en");
    }
    if (QLocale::system().name().startsWith(QLatin1String("id"))) {
        return QStringLiteral("id");
    }
    return QStringLiteral("en");
}

template <typename T>
void writeSetting(const char *key, const T &value)
{
    QSettings().setValue(QString::fromLatin1(key), value);
}

} // namespace

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    load();
    applyLanguage();

    // The "system" theme has to track the desktop's light/dark switch live;
    // QStyleHints emits this on every platform that reports a colour scheme.
    if (QGuiApplication::styleHints()) {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
                &AppSettings::darkChanged);
    }
}

void AppSettings::load()
{
    QSettings settings;
    const auto value = [&settings](const char *key, const QVariant &fallback) {
        return settings.value(QString::fromLatin1(key), fallback);
    };

    m_startupPage = normaliseStartupPage(value(kStartupPageKey, QStringLiteral("photos")).toString());
    m_lastPage = normalisePage(value(kLastPageKey, QStringLiteral("photos")).toString());
    m_confirmDestructive = value(kConfirmDestructiveKey, true).toBool();
    m_developerMode = value(kDeveloperModeKey, false).toBool();

    m_theme = normaliseTheme(value(kThemeKey, QStringLiteral("system")).toString());
    m_language = normaliseLanguage(value(kLanguageKey, QStringLiteral("system")).toString());
    m_accent = normaliseAccent(value(kAccentKey, QStringLiteral("blue")).toString());
    m_systemIcons = value(kSystemIconsKey, true).toBool();
    m_thumbnailSize = normaliseThumbnailSize(value(kThumbnailSizeKey, kDefaultThumbnailSize).toInt());
    m_gridSpacing = normaliseGridSpacing(value(kGridSpacingKey, 3).toInt());
    m_showSidebar = value(kShowSidebarKey, true).toBool();
    m_sidebarWidth = normaliseSidebarWidth(value(kSidebarWidthKey, 212).toInt());
    m_showDateHeaders = value(kShowDateHeadersKey, true).toBool();
    m_animateTransitions = value(kAnimateTransitionsKey, true).toBool();
    m_hoverEffects = value(kHoverEffectsKey, true).toBool();
    m_showPerfOverlay = value(kShowPerfOverlayKey, false).toBool();

    m_autoWatch = value(kAutoWatchKey, true).toBool();
    m_rescanOnStartup = value(kRescanOnStartupKey, false).toBool();
    m_followSymlinks = value(kFollowSymlinksKey, false).toBool();
    m_includeHidden = value(kIncludeHiddenKey, false).toBool();
    m_ignoreJunk = value(kIgnoreJunkKey, true).toBool();
    m_junkPatterns = normaliseIgnoredPatterns(
        value(kJunkPatternsKey, scan::defaultJunkPatterns()).toStringList());
    m_ignoredFolders = normaliseIgnoredFolders(
        value(kIgnoredFoldersKey, QStringList()).toStringList());
    m_ignoredPatterns = normaliseIgnoredPatterns(
        value(kIgnoredPatternsKey, QStringList()).toStringList());

    m_thumbnailMemoryMb = normaliseThumbnailMemoryMb(value(kThumbnailMemoryKey, 0).toInt());

    m_videoAutoplay = value(kVideoAutoplayKey, true).toBool();
    m_videoLoop = value(kVideoLoopKey, false).toBool();
    m_videoMute = value(kVideoMuteKey, false).toBool();
    m_videoVolume = normaliseVideoVolume(value(kVideoVolumeKey, 100).toInt());
}

QStringList AppSettings::accentNames()
{
    return {QStringLiteral("blue"),   QStringLiteral("teal"), QStringLiteral("green"),
            QStringLiteral("purple"), QStringLiteral("orange"), QStringLiteral("pink")};
}

QString AppSettings::normaliseTheme(const QString &theme)
{
    const QString lowered = theme.trimmed().toLower();
    if (lowered == QLatin1String("light") || lowered == QLatin1String("dark")) {
        return lowered;
    }
    return QStringLiteral("system");
}

QString AppSettings::normaliseLanguage(const QString &language)
{
    const QString lowered = language.trimmed().toLower();
    if (lowered == QLatin1String("id") || lowered == QLatin1String("en")) {
        return lowered;
    }
    return QStringLiteral("system");
}

QString AppSettings::normaliseAccent(const QString &accent)
{
    const QString lowered = accent.trimmed().toLower();
    if (accentNames().contains(lowered)) {
        return lowered;
    }
    return QStringLiteral("blue");
}

QString AppSettings::normaliseStartupPage(const QString &page)
{
    const QString lowered = page.trimmed().toLower();
    if (lowered == QLatin1String("photos") || lowered == QLatin1String("albums")
        || lowered == QLatin1String("settings")) {
        return lowered;
    }
    return QStringLiteral("last");
}

QString AppSettings::normalisePage(const QString &page)
{
    const QString lowered = page.trimmed().toLower();
    if (lowered == QLatin1String("photos") || lowered == QLatin1String("albums")
        || lowered == QLatin1String("settings")) {
        return lowered;
    }
    return QStringLiteral("photos");
}

int AppSettings::normaliseThumbnailSize(int size)
{
    const int clamped = qBound(kMinThumbnailSize, size, kMaxThumbnailSize);
    return (clamped / kThumbnailStep) * kThumbnailStep;
}

int AppSettings::normaliseGridSpacing(int spacing)
{
    return qBound(0, spacing, kMaxGridSpacing);
}

int AppSettings::normaliseSidebarWidth(int width)
{
    return qBound(kMinSidebarWidth, width, kMaxSidebarWidth);
}

int AppSettings::normaliseThumbnailMemoryMb(int mb)
{
    if (mb <= 0) {
        return 0; // adaptive
    }
    return qBound(kMinThumbnailMemoryMb, mb, kMaxThumbnailMemoryMb);
}

int AppSettings::normaliseVideoVolume(int volume)
{
    return qBound(0, volume, 100);
}

QStringList AppSettings::normaliseIgnoredFolders(const QStringList &folders)
{
    QStringList out;
    for (const QString &raw : folders) {
        const QString cleaned = QDir::cleanPath(raw.trimmed());
        if (cleaned.isEmpty() || cleaned == QLatin1String("/") || cleaned == QLatin1String(".")) {
            continue;
        }
        if (!out.contains(cleaned)) {
            out.append(cleaned);
        }
    }
    return out;
}

QStringList AppSettings::normaliseIgnoredPatterns(const QStringList &patterns)
{
    QStringList out;
    for (const QString &raw : patterns) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty() || out.contains(trimmed)) {
            continue;
        }
        out.append(trimmed);
    }
    return out;
}

void AppSettings::setStartupPage(const QString &page)
{
    const QString normalised = normaliseStartupPage(page);
    if (normalised == m_startupPage) {
        return;
    }
    m_startupPage = normalised;
    writeSetting(kStartupPageKey, m_startupPage);
    Q_EMIT startupPageChanged();
}

void AppSettings::setLastPage(const QString &page)
{
    const QString normalised = normalisePage(page);
    if (normalised == m_lastPage) {
        return;
    }
    m_lastPage = normalised;
    writeSetting(kLastPageKey, m_lastPage);
    Q_EMIT lastPageChanged();
}

void AppSettings::setConfirmDestructive(bool value)
{
    if (value == m_confirmDestructive) {
        return;
    }
    m_confirmDestructive = value;
    writeSetting(kConfirmDestructiveKey, m_confirmDestructive);
    Q_EMIT confirmDestructiveChanged();
}

void AppSettings::setDeveloperMode(bool value)
{
    if (value == m_developerMode) {
        return;
    }
    m_developerMode = value;
    writeSetting(kDeveloperModeKey, m_developerMode);
    Q_EMIT developerModeChanged();
}

void AppSettings::setTheme(const QString &theme)
{
    const QString normalised = normaliseTheme(theme);
    if (normalised == m_theme) {
        return;
    }
    m_theme = normalised;
    writeSetting(kThemeKey, m_theme);
    Q_EMIT themeChanged();
    Q_EMIT darkChanged();
}

void AppSettings::setLanguage(const QString &language)
{
    const QString normalised = normaliseLanguage(language);
    if (normalised == m_language) {
        return;
    }
    m_language = normalised;
    writeSetting(kLanguageKey, m_language);
    applyLanguage();
    Q_EMIT languageChanged();
}

void AppSettings::setAccent(const QString &accent)
{
    const QString normalised = normaliseAccent(accent);
    if (normalised == m_accent) {
        return;
    }
    m_accent = normalised;
    writeSetting(kAccentKey, m_accent);
    Q_EMIT accentChanged();
}

void AppSettings::setSystemIcons(bool value)
{
    if (value == m_systemIcons) {
        return;
    }
    m_systemIcons = value;
    writeSetting(kSystemIconsKey, m_systemIcons);
    Q_EMIT systemIconsChanged();
}

void AppSettings::setThumbnailSize(int size)
{
    const int normalised = normaliseThumbnailSize(size);
    if (normalised == m_thumbnailSize) {
        return;
    }
    m_thumbnailSize = normalised;
    writeSetting(kThumbnailSizeKey, m_thumbnailSize);
    Q_EMIT thumbnailSizeChanged();
}

void AppSettings::setGridSpacing(int spacing)
{
    const int normalised = normaliseGridSpacing(spacing);
    if (normalised == m_gridSpacing) {
        return;
    }
    m_gridSpacing = normalised;
    writeSetting(kGridSpacingKey, m_gridSpacing);
    Q_EMIT gridSpacingChanged();
}

void AppSettings::setShowSidebar(bool value)
{
    if (value == m_showSidebar) {
        return;
    }
    m_showSidebar = value;
    writeSetting(kShowSidebarKey, m_showSidebar);
    Q_EMIT showSidebarChanged();
}

void AppSettings::setSidebarWidth(int width)
{
    const int normalised = normaliseSidebarWidth(width);
    if (normalised == m_sidebarWidth) {
        return;
    }
    m_sidebarWidth = normalised;
    writeSetting(kSidebarWidthKey, m_sidebarWidth);
    Q_EMIT sidebarWidthChanged();
}

void AppSettings::setShowDateHeaders(bool value)
{
    if (value == m_showDateHeaders) {
        return;
    }
    m_showDateHeaders = value;
    writeSetting(kShowDateHeadersKey, m_showDateHeaders);
    Q_EMIT showDateHeadersChanged();
}

void AppSettings::setAnimateTransitions(bool value)
{
    if (value == m_animateTransitions) {
        return;
    }
    m_animateTransitions = value;
    writeSetting(kAnimateTransitionsKey, m_animateTransitions);
    Q_EMIT animateTransitionsChanged();
}

void AppSettings::setHoverEffects(bool value)
{
    if (value == m_hoverEffects) {
        return;
    }
    m_hoverEffects = value;
    writeSetting(kHoverEffectsKey, m_hoverEffects);
    Q_EMIT hoverEffectsChanged();
}

void AppSettings::setShowPerfOverlay(bool value)
{
    if (value == m_showPerfOverlay) {
        return;
    }
    m_showPerfOverlay = value;
    writeSetting(kShowPerfOverlayKey, m_showPerfOverlay);
    Q_EMIT showPerfOverlayChanged();
}

void AppSettings::setAutoWatch(bool value)
{
    if (value == m_autoWatch) {
        return;
    }
    m_autoWatch = value;
    writeSetting(kAutoWatchKey, m_autoWatch);
    Q_EMIT autoWatchChanged();
}

void AppSettings::setRescanOnStartup(bool value)
{
    if (value == m_rescanOnStartup) {
        return;
    }
    m_rescanOnStartup = value;
    writeSetting(kRescanOnStartupKey, m_rescanOnStartup);
    Q_EMIT rescanOnStartupChanged();
}

void AppSettings::setFollowSymlinks(bool value)
{
    if (value == m_followSymlinks) {
        return;
    }
    m_followSymlinks = value;
    writeSetting(kFollowSymlinksKey, m_followSymlinks);
    Q_EMIT followSymlinksChanged();
}

void AppSettings::setIncludeHidden(bool value)
{
    if (value == m_includeHidden) {
        return;
    }
    m_includeHidden = value;
    writeSetting(kIncludeHiddenKey, m_includeHidden);
    Q_EMIT includeHiddenChanged();
}

void AppSettings::setIgnoreJunk(bool value)
{
    if (value == m_ignoreJunk) {
        return;
    }
    m_ignoreJunk = value;
    writeSetting(kIgnoreJunkKey, m_ignoreJunk);
    Q_EMIT ignoreJunkChanged();
}

void AppSettings::setJunkPatterns(const QStringList &patterns)
{
    const QStringList normalised = normaliseIgnoredPatterns(patterns);
    if (normalised == m_junkPatterns) {
        return;
    }
    m_junkPatterns = normalised;
    writeSetting(kJunkPatternsKey, m_junkPatterns);
    Q_EMIT junkPatternsChanged();
}

void AppSettings::addJunkPattern(const QString &pattern)
{
    const QString trimmed = pattern.trimmed();
    if (trimmed.isEmpty() || m_junkPatterns.contains(trimmed)) {
        return;
    }
    m_junkPatterns.append(trimmed);
    writeSetting(kJunkPatternsKey, m_junkPatterns);
    Q_EMIT junkPatternsChanged();
}

void AppSettings::removeJunkPattern(const QString &pattern)
{
    if (!m_junkPatterns.removeOne(pattern.trimmed())) {
        return;
    }
    writeSetting(kJunkPatternsKey, m_junkPatterns);
    Q_EMIT junkPatternsChanged();
}

void AppSettings::resetJunkPatterns()
{
    setJunkPatterns(scan::defaultJunkPatterns());
}

void AppSettings::addIgnoredFolder(const QString &folder)
{
    const QString cleaned = QDir::cleanPath(folder.trimmed());
    if (cleaned.isEmpty() || cleaned == QLatin1String("/") || cleaned == QLatin1String(".")
        || m_ignoredFolders.contains(cleaned)) {
        return;
    }
    m_ignoredFolders.append(cleaned);
    writeSetting(kIgnoredFoldersKey, m_ignoredFolders);
    Q_EMIT ignoredFoldersChanged();
}

void AppSettings::removeIgnoredFolder(const QString &folder)
{
    const QString cleaned = QDir::cleanPath(folder.trimmed());
    if (!m_ignoredFolders.removeOne(cleaned)) {
        return;
    }
    writeSetting(kIgnoredFoldersKey, m_ignoredFolders);
    Q_EMIT ignoredFoldersChanged();
}

void AppSettings::setIgnoredPatterns(const QStringList &patterns)
{
    const QStringList normalised = normaliseIgnoredPatterns(patterns);
    if (normalised == m_ignoredPatterns) {
        return;
    }
    m_ignoredPatterns = normalised;
    writeSetting(kIgnoredPatternsKey, m_ignoredPatterns);
    Q_EMIT ignoredPatternsChanged();
}

void AppSettings::setIgnoredPatternsFromText(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("[,;\\n]"));
    setIgnoredPatterns(text.split(separators, Qt::SkipEmptyParts));
}

void AppSettings::setThumbnailMemoryMb(int mb)
{
    const int normalised = normaliseThumbnailMemoryMb(mb);
    if (normalised == m_thumbnailMemoryMb) {
        return;
    }
    m_thumbnailMemoryMb = normalised;
    writeSetting(kThumbnailMemoryKey, m_thumbnailMemoryMb);
    Q_EMIT thumbnailMemoryMbChanged();
}

void AppSettings::setVideoAutoplay(bool value)
{
    if (value == m_videoAutoplay) {
        return;
    }
    m_videoAutoplay = value;
    writeSetting(kVideoAutoplayKey, m_videoAutoplay);
    Q_EMIT videoAutoplayChanged();
}

void AppSettings::setVideoLoop(bool value)
{
    if (value == m_videoLoop) {
        return;
    }
    m_videoLoop = value;
    writeSetting(kVideoLoopKey, m_videoLoop);
    Q_EMIT videoLoopChanged();
}

void AppSettings::setVideoMute(bool value)
{
    if (value == m_videoMute) {
        return;
    }
    m_videoMute = value;
    writeSetting(kVideoMuteKey, m_videoMute);
    Q_EMIT videoMuteChanged();
}

void AppSettings::setVideoVolume(int volume)
{
    const int normalised = normaliseVideoVolume(volume);
    if (normalised == m_videoVolume) {
        return;
    }
    m_videoVolume = normalised;
    writeSetting(kVideoVolumeKey, m_videoVolume);
    Q_EMIT videoVolumeChanged();
}

bool AppSettings::dark() const
{
    if (m_theme == QLatin1String("dark")) {
        return true;
    }
    if (m_theme == QLatin1String("light")) {
        return false;
    }
    // "system": Unknown is treated as light, which matches most desktops and
    // keeps the first frame on a plain GTK/Qt default from flashing.
    if (QGuiApplication::styleHints()) {
        return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    }
    return false;
}

void AppSettings::applyLanguage()
{
    if (m_translatorInstalled) {
        QCoreApplication::removeTranslator(&m_translator);
        m_translatorInstalled = false;
    }

    const QString stem = localeStem(m_language);
    if (stem == QLatin1String("en")) {
        // English is the source language; removing the translator restores it.
        return;
    }

    // qt_add_translations() embeds the compiled catalogs under :/i18n/.
    if (m_translator.load(QStringLiteral(":/i18n/kin-galeri_%1.qm").arg(stem))) {
        QCoreApplication::installTranslator(&m_translator);
        m_translatorInstalled = true;
    }
}

} // namespace kg
