#pragma once
#include "semantic_fs/monitoring/monitoring_types.h"
#include <memory>
#include <variant>
namespace semantic_fs::monitoring {
enum class FileSystemEntryKind { RegularFile, Directory, LinkOrReparse, Other };
enum class FsErrorCode { NotFound, AccessDenied, InvalidMetadata, InvalidEncoding, Io };
struct FsError { FsErrorCode code; }; struct FileSystemEntry { AbsolutePath path; FileSystemEntryKind kind; };
using ListResult = std::variant<std::vector<FileSystemEntry>, FsError>; using MetadataResult = std::variant<FileMetadata, FsError>;
class IFileSystemView { public: virtual ~IFileSystemView() = default; virtual ListResult list(const AbsolutePath& directory) const = 0; virtual MetadataResult metadata(const AbsolutePath& path) const = 0; };
#ifdef _WIN32
std::unique_ptr<IFileSystemView> makeWindowsFileSystemView();
#endif
} // namespace semantic_fs::monitoring