#include "semantic_fs/core/file_document.h"

#include <utility>

namespace semantic_fs::core {

FileDocument::FileDocument(std::filesystem::path path, FileType type)
    : path_(std::move(path)),
      fileType_(type)
{
    refreshMetadataFromDisk();
}

const std::filesystem::path& FileDocument::path() const
{
    return path_;
}

std::filesystem::path FileDocument::absolutePath() const
{
    if (path_.empty()) {
        return {};
    }

    return std::filesystem::absolute(path_);
}

std::filesystem::path FileDocument::directory() const
{
    return path_.parent_path();
}

std::string FileDocument::fileName() const
{
    return path_.filename().string();
}

std::string FileDocument::extension() const
{
    return path_.extension().string();
}

FileDocument::FileType FileDocument::fileType() const
{
    return fileType_;
}

const std::string& FileDocument::hash() const
{
    return hash_;
}

std::uintmax_t FileDocument::sizeBytes() const
{
    return sizeBytes_;
}

std::filesystem::file_time_type FileDocument::modifiedAt() const
{
    return modifiedAt_;
}

const std::vector<FileDocument::ExtractedSegment>& FileDocument::contexts() const
{
    return contexts_;
}

bool FileDocument::hasContexts() const
{
    return !contexts_.empty();
}

std::size_t FileDocument::contextCount() const
{
    return contexts_.size();
}

void FileDocument::setPath(std::filesystem::path path)
{
    path_ = std::move(path);
}

void FileDocument::setFileType(FileType type)
{
    fileType_ = type;
}

void FileDocument::setHash(std::string hash)
{
    hash_ = std::move(hash);
}

void FileDocument::setSizeBytes(std::uintmax_t sizeBytes)
{
    sizeBytes_ = sizeBytes;
}

void FileDocument::setModifiedAt(std::filesystem::file_time_type modifiedAt)
{
    modifiedAt_ = modifiedAt;
}

void FileDocument::addContext(ExtractedSegment segment)
{
    contexts_.push_back(std::move(segment));
}

void FileDocument::setContexts(std::vector<ExtractedSegment> contexts)
{
    contexts_ = std::move(contexts);
}

void FileDocument::clearContexts()
{
    contexts_.clear();
}

void FileDocument::refreshMetadataFromDisk()
{
    if (path_.empty() || !std::filesystem::exists(path_)) {
        sizeBytes_ = 0;
        modifiedAt_ = {};
        return;
    }

    if (std::filesystem::is_regular_file(path_)) {
        sizeBytes_ = std::filesystem::file_size(path_);
    }

    modifiedAt_ = std::filesystem::last_write_time(path_);
}

} // namespace semantic_fs::core
