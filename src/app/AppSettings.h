#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTranslator>

#include "core/indexer/ScanPolicy.h"

namespace kg {

/**
 * User-facing preferences that outlive a run.
 *
 * Stored with QSettings (org/app names are set in main.cpp) rather than the
 * SQLite library, because they describe the *application*, not the photo
 * collection - moving `library.db` between machines should not drag the theme
 * along with it.
 *
 * The class is deliberately grouped to mirror the settings page: general
 * (startup + destructive-action guards), interface (theme, accent, layout),
 * library (indexing + watching), thumbnails and playback. Values are normalised
 * on both load and write so a hand-edited config file can never put the UI in a
 * state that has no matching control.
 */
class AppSettings : public QObject
{
    Q_OBJECT

    // --- General ----------------------------------------------------------
    Q_PROPERTY(QString startupPage READ startupPage WRITE setStartupPage NOTIFY startupPageChanged)
    Q_PROPERTY(QString lastPage READ lastPage WRITE setLastPage NOTIFY lastPageChanged)
    Q_PROPERTY(bool confirmDestructive READ confirmDestructive WRITE setConfirmDestructive NOTIFY confirmDestructiveChanged)
    /** When on, exposes developer-only surfaces (benchmark seeding, perf overlay,
     *  storage internals in the status bar). Off for normal users. */
    Q_PROPERTY(bool developerMode READ developerMode WRITE setDeveloperMode NOTIFY developerModeChanged)

    // --- Interface --------------------------------------------------------
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY accentChanged)
    /** Use desktop-theme icons for the sidebar/settings instead of text glyphs. */
    Q_PROPERTY(bool systemIcons READ systemIcons WRITE setSystemIcons NOTIFY systemIconsChanged)
    Q_PROPERTY(int thumbnailSize READ thumbnailSize WRITE setThumbnailSize NOTIFY thumbnailSizeChanged)
    Q_PROPERTY(int gridSpacing READ gridSpacing WRITE setGridSpacing NOTIFY gridSpacingChanged)
    Q_PROPERTY(bool showSidebar READ showSidebar WRITE setShowSidebar NOTIFY showSidebarChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth WRITE setSidebarWidth NOTIFY sidebarWidthChanged)
    Q_PROPERTY(bool showDateHeaders READ showDateHeaders WRITE setShowDateHeaders NOTIFY showDateHeadersChanged)
    Q_PROPERTY(bool animateTransitions READ animateTransitions WRITE setAnimateTransitions NOTIFY animateTransitionsChanged)
    Q_PROPERTY(bool hoverEffects READ hoverEffects WRITE setHoverEffects NOTIFY hoverEffectsChanged)
    Q_PROPERTY(bool showPerfOverlay READ showPerfOverlay WRITE setShowPerfOverlay NOTIFY showPerfOverlayChanged)

    // --- Library ----------------------------------------------------------
    Q_PROPERTY(bool autoWatch READ autoWatch WRITE setAutoWatch NOTIFY autoWatchChanged)
    Q_PROPERTY(bool rescanOnStartup READ rescanOnStartup WRITE setRescanOnStartup NOTIFY rescanOnStartupChanged)
    Q_PROPERTY(bool followSymlinks READ followSymlinks WRITE setFollowSymlinks NOTIFY followSymlinksChanged)
    Q_PROPERTY(bool includeHidden READ includeHidden WRITE setIncludeHidden NOTIFY includeHiddenChanged)
    Q_PROPERTY(bool ignoreJunk READ ignoreJunk WRITE setIgnoreJunk NOTIFY ignoreJunkChanged)
    /** Editable built-in junk patterns (node_modules, caches, toolchains...).
     *  Applied only while ignoreJunk is on. */
    Q_PROPERTY(QStringList junkPatterns READ junkPatterns NOTIFY junkPatternsChanged)
    /** Absolute folders the user excluded from scanning (and their subtrees). */
    Q_PROPERTY(QStringList ignoredFolders READ ignoredFolders NOTIFY ignoredFoldersChanged)
    /** Glob patterns matched against a directory name during scanning. */
    Q_PROPERTY(QStringList ignoredPatterns READ ignoredPatterns NOTIFY ignoredPatternsChanged)

    // --- Thumbnails -------------------------------------------------------
    Q_PROPERTY(int thumbnailMemoryMb READ thumbnailMemoryMb WRITE setThumbnailMemoryMb NOTIFY thumbnailMemoryMbChanged)

    // --- Playback ---------------------------------------------------------
    Q_PROPERTY(bool videoAutoplay READ videoAutoplay WRITE setVideoAutoplay NOTIFY videoAutoplayChanged)
    Q_PROPERTY(bool videoLoop READ videoLoop WRITE setVideoLoop NOTIFY videoLoopChanged)
    Q_PROPERTY(bool videoMute READ videoMute WRITE setVideoMute NOTIFY videoMuteChanged)
    Q_PROPERTY(int videoVolume READ videoVolume WRITE setVideoVolume NOTIFY videoVolumeChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);

    QString startupPage() const { return m_startupPage; }
    void setStartupPage(const QString &page);
    QString lastPage() const { return m_lastPage; }
    void setLastPage(const QString &page);

    bool confirmDestructive() const { return m_confirmDestructive; }
    void setConfirmDestructive(bool value);

    bool developerMode() const { return m_developerMode; }
    void setDeveloperMode(bool value);

    QString theme() const { return m_theme; }
    void setTheme(const QString &theme);

    /// "system", "en" or "id".
    QString language() const { return m_language; }
    void setLanguage(const QString &language);

    bool dark() const;

    /// Accent colour name ("blue", "teal", "green", "purple", "orange", "pink").
    QString accent() const { return m_accent; }
    void setAccent(const QString &accent);

    bool systemIcons() const { return m_systemIcons; }
    void setSystemIcons(bool value);

    /// Target thumbnail edge in pixels, clamped to [80, 384] and snapped to an
    /// 8px grid so the toolbar's +/- steps and the settings slider agree.
    int thumbnailSize() const { return m_thumbnailSize; }
    void setThumbnailSize(int size);

    int gridSpacing() const { return m_gridSpacing; }
    void setGridSpacing(int spacing);

    bool showSidebar() const { return m_showSidebar; }
    void setShowSidebar(bool value);

    int sidebarWidth() const { return m_sidebarWidth; }
    void setSidebarWidth(int width);

    bool showDateHeaders() const { return m_showDateHeaders; }
    void setShowDateHeaders(bool value);

    bool animateTransitions() const { return m_animateTransitions; }
    void setAnimateTransitions(bool value);

    bool hoverEffects() const { return m_hoverEffects; }
    void setHoverEffects(bool value);

    bool showPerfOverlay() const { return m_showPerfOverlay; }
    void setShowPerfOverlay(bool value);

    bool autoWatch() const { return m_autoWatch; }
    void setAutoWatch(bool value);

    bool rescanOnStartup() const { return m_rescanOnStartup; }
    void setRescanOnStartup(bool value);

    bool followSymlinks() const { return m_followSymlinks; }
    void setFollowSymlinks(bool value);

    bool includeHidden() const { return m_includeHidden; }
    void setIncludeHidden(bool value);

    bool ignoreJunk() const { return m_ignoreJunk; }
    void setIgnoreJunk(bool value);

    QStringList junkPatterns() const { return m_junkPatterns; }
    void setJunkPatterns(const QStringList &patterns);
    Q_INVOKABLE void addJunkPattern(const QString &pattern);
    Q_INVOKABLE void removeJunkPattern(const QString &pattern);
    /// Restores the list shipped with the app.
    Q_INVOKABLE void resetJunkPatterns();

    QStringList ignoredFolders() const { return m_ignoredFolders; }
    Q_INVOKABLE void addIgnoredFolder(const QString &folder);
    Q_INVOKABLE void removeIgnoredFolder(const QString &folder);

    QStringList ignoredPatterns() const { return m_ignoredPatterns; }
    void setIgnoredPatterns(const QStringList &patterns);
    /// Parses a comma/semicolon/newline separated list, for the settings text box.
    Q_INVOKABLE void setIgnoredPatternsFromText(const QString &text);

    /// In-memory decoded-thumbnail budget in MB. 0 means adaptive (sized from
    /// the visible grid); otherwise clamped to [32, 2048].
    int thumbnailMemoryMb() const { return m_thumbnailMemoryMb; }
    void setThumbnailMemoryMb(int mb);

    bool videoAutoplay() const { return m_videoAutoplay; }
    void setVideoAutoplay(bool value);

    bool videoLoop() const { return m_videoLoop; }
    void setVideoLoop(bool value);

    bool videoMute() const { return m_videoMute; }
    void setVideoMute(bool value);

    /// 0-100, matching mpv's own volume scale.
    int videoVolume() const { return m_videoVolume; }
    void setVideoVolume(int volume);

    static QString normaliseTheme(const QString &theme);
    static QString normaliseLanguage(const QString &language);
    static QString normaliseAccent(const QString &accent);
    static QString normaliseStartupPage(const QString &page);
    static QString normalisePage(const QString &page);
    static int normaliseThumbnailSize(int size);
    static int normaliseGridSpacing(int spacing);
    static int normaliseSidebarWidth(int width);
    static int normaliseThumbnailMemoryMb(int mb);
    static int normaliseVideoVolume(int volume);
    /// Cleans/dedupes a folder list for the ignore set.
    static QStringList normaliseIgnoredFolders(const QStringList &folders);
    /// Trims/dedupes glob patterns, dropping empties.
    static QStringList normaliseIgnoredPatterns(const QStringList &patterns);

    /// Accent names offered by the settings page, in display order.
    static QStringList accentNames();

signals:
    void startupPageChanged();
    void lastPageChanged();
    void confirmDestructiveChanged();
    void developerModeChanged();
    void themeChanged();
    void languageChanged();
    void darkChanged();
    void accentChanged();
    void systemIconsChanged();
    void thumbnailSizeChanged();
    void gridSpacingChanged();
    void showSidebarChanged();
    void sidebarWidthChanged();
    void showDateHeadersChanged();
    void animateTransitionsChanged();
    void hoverEffectsChanged();
    void showPerfOverlayChanged();
    void autoWatchChanged();
    void rescanOnStartupChanged();
    void followSymlinksChanged();
    void includeHiddenChanged();
    void ignoreJunkChanged();
    void junkPatternsChanged();
    void ignoredFoldersChanged();
    void ignoredPatternsChanged();
    void thumbnailMemoryMbChanged();
    void videoAutoplayChanged();
    void videoLoopChanged();
    void videoMuteChanged();
    void videoVolumeChanged();

private:
    void load();
    void applyLanguage();

    QTranslator m_translator;
    QString m_startupPage = QStringLiteral("photos");
    QString m_lastPage = QStringLiteral("photos");
    bool m_confirmDestructive = true;
    bool m_developerMode = false;
    QString m_theme = QStringLiteral("system");
    QString m_language = QStringLiteral("system");
    QString m_accent = QStringLiteral("blue");
    bool m_systemIcons = true;
    int m_thumbnailSize = 168;
    int m_gridSpacing = 3;
    bool m_showSidebar = true;
    int m_sidebarWidth = 212;
    bool m_showDateHeaders = true;
    bool m_animateTransitions = true;
    bool m_hoverEffects = true;
    bool m_showPerfOverlay = false;
    bool m_autoWatch = true;
    bool m_rescanOnStartup = false;
    bool m_followSymlinks = false;
    bool m_includeHidden = false;
    bool m_ignoreJunk = true;
    QStringList m_junkPatterns = scan::defaultJunkPatterns();
    QStringList m_ignoredFolders;
    QStringList m_ignoredPatterns;
    int m_thumbnailMemoryMb = 0;
    bool m_videoAutoplay = true;
    bool m_videoLoop = false;
    bool m_videoMute = false;
    int m_videoVolume = 100;
    bool m_translatorInstalled = false;
};

} // namespace kg
