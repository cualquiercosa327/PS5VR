#pragma once
/*
 * The PS5VR app's stand-in for the engine's Application singleton.
 *
 * The vendored PlaybackController asks the engine's app for its settings, chapter
 * metadata and file browser. The PS5VR Player has none of those: every call
 * site already handles a missing service (default decoder choice, 30 s chapter
 * jumps, no folder auto-advance), so this answers "none" for each.
 */
#include "eng/interfaces/IFileSystemBrowser.hpp"
#include "eng/interfaces/IPlaybackController.hpp"
#include "eng/interfaces/IMediaMetadataService.hpp"
#include "eng/interfaces/ISettingsService.hpp"

namespace engine {

class Application {
public:
    static Application& getInstance() {
        static Application app;
        return app;
    }
    ISettingsService* getSettingsService() const { return nullptr; }
    IMediaMetadataService* getMediaMetadataService() const { return nullptr; }
    IFileSystemBrowser* getFileSystemBrowser() const { return nullptr; }

    /* The PS5VR Player's controller (player.cpp), for engine helpers that
     * restart playback, e.g. on a subtitle track switch. */
    IPlaybackController* getPlaybackController() const { return m_playback; }
    void setPlaybackController(IPlaybackController* pb) { m_playback = pb; }

private:
    IPlaybackController* m_playback = nullptr;
};

} // namespace engine
