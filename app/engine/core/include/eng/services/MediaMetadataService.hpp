#ifndef ENG_MEDIA_METADATA_SERVICE_HPP
#define ENG_MEDIA_METADATA_SERVICE_HPP

#include "eng/interfaces/IMediaMetadataService.hpp"
#include <vector>

namespace engine {

class MediaMetadataService : public IMediaMetadataService {
public:
    MediaMetadataService() = default;
    ~MediaMetadataService() override = default;

    MediaMetadataInfo extractBasicMetadata(const std::string& filePath) override;
    MediaMetadataInfo extractMetadataFromFormat(AVFormatContext* formatContext, const std::string& filePath) override;

    void loadChapters(AVFormatContext* formatContext) override;
    void clearChapters() override;
    size_t getChapterCount() const override { return m_chapters.size(); }
    const ChapterInfo* getChapter(size_t index) const override;
    int getChapterIndexAtPosition(double positionSeconds) const override;

    void cleanMediaTitle(const std::string& filePath, std::string& outTitle, std::string& outCategory) override;
    std::string formatDuration(double durationSeconds) override;
    std::string formatFileSize(int64_t bytes) override;
    std::string formatRemainingTime(double currentPosition, double duration) override;
    int calculateProgressPermille(double currentPosition, double duration) override;

    bool exportCompatibilityReport(const std::string& outputPath,
                                   const MediaMetadataInfo& metadata,
                                   double playbackPosition,
                                   int recentCount,
                                   int favoriteCount,
                                   bool isPlaying) override;

private:
    std::vector<ChapterInfo> m_chapters;
};

} // namespace engine

#endif // ENG_MEDIA_METADATA_SERVICE_HPP
