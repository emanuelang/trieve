#include "semantic_fs/monitoring/i_file_watcher.h"
#include "windows_file_watcher_parser.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <span>
#include <thread>

namespace semantic_fs::monitoring {
namespace {
constexpr std::size_t kCapacity = 64;

std::optional<std::wstring> wide(std::string_view value)
{
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!size) return std::nullopt;
    std::wstring result(size, L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size) ? std::optional<std::wstring>{std::move(result)} : std::nullopt;
}

std::optional<std::string> utf8(const wchar_t* value, int length)
{
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, length, nullptr, 0, nullptr, nullptr);
    if (!size) return std::nullopt;
    std::string result(size, '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, length, result.data(), size, nullptr, nullptr) ? std::optional<std::string>{std::move(result)} : std::nullopt;
}

bool isSafeRelativeName(std::wstring_view name)
{
    if (name.empty() || name.starts_with(L'\\') || name.starts_with(L'/') || name.find(L'\0') != std::wstring_view::npos) return false;
    std::size_t start{};
    while (start < name.size()) {
        const auto end = name.find_first_of(L"\\/", start);
        const auto component = name.substr(start, end - start);
        if (component.empty() || component == L"." || component == L".." || component.find(L':') != std::wstring_view::npos) return false;
        if (end == std::wstring_view::npos) break;
        start = end + 1;
    }
    return true;
}

std::optional<AbsolutePath> pathUnderRoot(const WatchRootConfig& root, std::wstring_view name)
{
    if (!isSafeRelativeName(name)) return std::nullopt;
    const auto utf8Name = utf8(name.data(), static_cast<int>(name.size()));
    if (!utf8Name) return std::nullopt;
    std::string normalizedName = *utf8Name;
    std::replace(normalizedName.begin(), normalizedName.end(), '\\', '/');
    std::string value = root.root.utf8;
    while (!value.empty() && (value.back() == '/' || value.back() == '\\')) value.pop_back();
    if (value.empty()) return std::nullopt;
    return AbsolutePath{std::move(value) + "/" + normalizedName};
}

WatcherEvent eventFor(const WatchRootConfig& root, DWORD action, AbsolutePath path)
{
    auto kind = WatcherEventKind::Modified;
    if (action == FILE_ACTION_ADDED) kind = WatcherEventKind::Created;
    else if (action == FILE_ACTION_REMOVED || action == FILE_ACTION_RENAMED_OLD_NAME) kind = WatcherEventKind::Removed;
    return {root.rootId, kind, std::move(path), {}};
}

struct State final : IWatcherSession, std::enable_shared_from_this<State> {
    State(WatchRootConfig value, IWatcherIngressSink& valueSink, std::wstring nativeRoot)
        : root(std::move(value)), sink(valueSink), rootWide(std::move(nativeRoot)) {}
    ~State() override { stopAndJoin(); }

    BarrierRequestOutcome requestBarrier() noexcept override
    {
        std::lock_guard lock(mutex);
        if (stopped) return {BarrierRequestStatus::Stopped, {}};
        if (queue.size() == kCapacity) return {BarrierRequestStatus::Busy, {}};
        const auto id = ++barrier;
        queue.emplace_back(BarrierReached{id, sequence, dirtyEpoch});
        wake.notify_one();
        return {BarrierRequestStatus::Accepted, id};
    }
    CancelOutcome requestCancellation() noexcept override
    {
        if (stopping.exchange(true)) return stopped ? CancelOutcome::AlreadyStopped : CancelOutcome::AlreadyRequested;
        markDirty(DirtyReason::Cancellation);
        if (directory != INVALID_HANDLE_VALUE) CancelIoEx(directory, &overlapped);
        wake.notify_all();
        return CancelOutcome::Requested;
    }
    StopOutcome stopAndJoin() noexcept override
    {
        const bool wasStopped = stopped.exchange(true);
        stopping = true;
        if (directory != INVALID_HANDLE_VALUE) CancelIoEx(directory, &overlapped);
        wake.notify_all();
        if (io.joinable()) io.join();
        if (dispatch.joinable()) dispatch.join();
        if (directory != INVALID_HANDLE_VALUE) { CloseHandle(directory); directory = INVALID_HANDLE_VALUE; }
        if (overlapped.hEvent) { CloseHandle(overlapped.hEvent); overlapped.hEvent = nullptr; }
        return wasStopped ? StopOutcome::AlreadyStopped : StopOutcome::Stopped;
    }
    void markDirty(DirtyReason reason)
    {
        std::lock_guard lock(mutex);
        dirtyReasons |= static_cast<std::uint32_t>(reason);
        ++dirtyEpoch;
        dirtyPending = true;
        wake.notify_one();
    }
    void enqueue(WatcherEvent event)
    {
        std::lock_guard lock(mutex);
        if (stopping || queue.size() == kCapacity) { dirtyReasons |= static_cast<std::uint32_t>(DirtyReason::QueueSaturation); ++dirtyEpoch; dirtyPending = true; wake.notify_one(); return; }
        queue.emplace_back(WatcherRecord{++sequence, std::move(event)});
        wake.notify_one();
    }
    void dispatchLoop()
    {
        for (;;) {
            std::optional<WatcherIngress> item;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopped || !queue.empty() || dirtyPending; });
                if (queue.empty() && dirtyPending) { item.emplace(RootDirty{root.rootId, dirtyEpoch, dirtyReasons}); dirtyPending = false; }
                else if (!queue.empty()) { item.emplace(std::move(queue.front())); queue.pop_front(); }
                else if (stopped) return;
                else continue;
            }
            if (!stopped && sink.accept(std::move(*item)) != IngressDelivery::Accepted) markDirty(DirtyReason::SinkRefusal);
        }
    }
    void readLoop()
    {
        std::array<std::byte, 8192> buffer{};
        while (!stopping) {
            ResetEvent(overlapped.hEvent);
            DWORD ignored{};
            const auto issued = ReadDirectoryChangesW(directory, buffer.data(), static_cast<DWORD>(buffer.size()), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                &ignored, &overlapped, nullptr);
            if (!issued && GetLastError() != ERROR_IO_PENDING) { markDirty(DirtyReason::NativeFailure); break; }
            {
                std::lock_guard lock(mutex);
                readArmed = true;
            }
            wake.notify_all();
            const auto waited = WaitForSingleObject(overlapped.hEvent, INFINITE);
            if (stopping) break;
            DWORD bytes{};
            if (waited != WAIT_OBJECT_0 || !GetOverlappedResult(directory, &overlapped, &bytes, FALSE) || bytes == 0) { markDirty(DirtyReason::OsOverflow); continue; }
            parse(buffer.data(), bytes);
        }
    }
    void parse(const std::byte* bytes, DWORD byteCount)
    {
        const auto termination = stopping.load() ? detail::WindowsWatcherDecodeTermination::Cancelled : detail::WindowsWatcherDecodeTermination::BatchComplete;
        auto decoded = detail::decodeWindowsWatcherRecords(root, std::span(bytes, static_cast<std::size_t>(byteCount)), termination);
        for (auto& event : decoded.events) enqueue(std::move(event));
        if (decoded.dirty) markDirty(DirtyReason::DecodeGap);
    }

    WatchRootConfig root;
    IWatcherIngressSink& sink;
    std::wstring rootWide;
    HANDLE directory{INVALID_HANDLE_VALUE};
    OVERLAPPED overlapped{};
    std::thread io, dispatch;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<WatcherIngress> queue;
    std::atomic_bool stopping{false}, stopped{false};
    WatcherSequence sequence{};
    BarrierId barrier{};
    GapEpoch dirtyEpoch{};
    std::uint32_t dirtyReasons{};
    bool dirtyPending{};
    bool readArmed{};
};

class WindowsFileWatcher final : public IFileWatcher {
public:
    StopOutcome stopAndJoin() noexcept override
    {
        std::shared_ptr<State> active;
        {
            std::lock_guard lock(mutex);
            active = state.lock();
        }
        return active ? active->stopAndJoin() : StopOutcome::AlreadyStopped;
    }
    WatcherStartOutcome start(const WatchRootConfig& root, IWatcherIngressSink& sink) override
    {
        std::lock_guard lock(mutex);
        if (auto active = state.lock()) {
            if (!active->stopped) return {(active->root.root.utf8 == root.root.utf8 && &active->sink == &sink) ? WatcherStartStatus::AlreadyStarted : WatcherStartStatus::Busy, active};
            state.reset();
        }
        auto native = wide(root.root.utf8);
        if (!native) return {WatcherStartStatus::InvalidConfig, {}};
        auto session = std::make_shared<State>(root, sink, std::move(*native));
        session->directory = CreateFileW(session->rootWide.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (session->directory == INVALID_HANDLE_VALUE) return {WatcherStartStatus::RootUnavailable, {}};
        session->overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!session->overlapped.hEvent) return {WatcherStartStatus::NativeFailure, {}};
        session->dispatch = std::thread([session] { session->dispatchLoop(); });
        session->io = std::thread([session] { session->readLoop(); });
        {
            std::unique_lock sessionLock(session->mutex);
            if (!session->wake.wait_for(sessionLock, std::chrono::seconds(3), [&] { return session->readArmed || session->stopping; })) {
                sessionLock.unlock();
                session->stopAndJoin();
                return {WatcherStartStatus::NativeFailure, {}};
            }
        }
        state = session;
        return {WatcherStartStatus::Started, session};
    }
private:
    std::mutex mutex;
    std::weak_ptr<State> state;
};
} // namespace

namespace detail {
DecodedWindowsWatcherRecords decodeWindowsWatcherRecords(
    const WatchRootConfig& root,
    std::span<const std::byte> bytes,
    WindowsWatcherDecodeTermination termination)
{
    DecodedWindowsWatcherRecords result;
    result.dirty = termination == WindowsWatcherDecodeTermination::Cancelled;
    std::optional<WatcherEvent> pendingOld;
    const auto degradePendingOld = [&] {
        if (!pendingOld) return;
        result.events.push_back(std::move(*pendingOld));
        pendingOld.reset();
        result.dirty = true;
    };

    std::size_t offset{};
    while (offset < bytes.size()) {
        constexpr auto fixedSize = offsetof(FILE_NOTIFY_INFORMATION, FileName);
        if (bytes.size() - offset < fixedSize) {
            degradePendingOld();
            result.dirty = true;
            return result;
        }
        const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(bytes.data() + offset);
        const auto nameBytes = static_cast<std::size_t>(info->FileNameLength);
        if (nameBytes % sizeof(wchar_t) || nameBytes > bytes.size() - offset - fixedSize) {
            degradePendingOld();
            result.dirty = true;
            return result;
        }
        const auto name = std::wstring_view(info->FileName, nameBytes / sizeof(wchar_t));
        const auto path = pathUnderRoot(root, name);
        if (!path) {
            degradePendingOld();
            result.dirty = true;
            return result;
        }

        switch (info->Action) {
        case FILE_ACTION_RENAMED_OLD_NAME:
            degradePendingOld();
            pendingOld = eventFor(root, info->Action, *path);
            break;
        case FILE_ACTION_RENAMED_NEW_NAME:
            if (pendingOld && termination != WindowsWatcherDecodeTermination::Cancelled) {
                auto renamed = eventFor(root, info->Action, *path);
                renamed.kind = WatcherEventKind::Renamed;
                renamed.previousPath = pendingOld->path;
                result.events.push_back(std::move(renamed));
                pendingOld.reset();
            } else {
                degradePendingOld();
                result.events.push_back(eventFor(root, info->Action, *path));
                result.events.back().kind = WatcherEventKind::Created;
                result.dirty = true;
            }
            break;
        case FILE_ACTION_ADDED:
        case FILE_ACTION_REMOVED:
        case FILE_ACTION_MODIFIED:
            degradePendingOld();
            result.events.push_back(eventFor(root, info->Action, *path));
            break;
        default:
            degradePendingOld();
            result.dirty = true;
            break;
        }

        const auto recordSize = fixedSize + nameBytes;
        if (!info->NextEntryOffset) {
            if (recordSize < bytes.size() - offset
                && std::any_of(bytes.begin() + static_cast<std::ptrdiff_t>(offset + recordSize), bytes.end(), [](std::byte value) { return value != std::byte{}; })) {
                degradePendingOld();
                result.dirty = true;
            }
            degradePendingOld();
            return result;
        }
        const auto nextOffset = static_cast<std::size_t>(info->NextEntryOffset);
        if (nextOffset < recordSize || nextOffset > bytes.size() - offset) {
            degradePendingOld();
            result.dirty = true;
            return result;
        }
        offset += nextOffset;
    }

    degradePendingOld();
    result.dirty = true;
    return result;
}
} // namespace detail

std::unique_ptr<IFileWatcher> makeWindowsFileWatcher() { return std::make_unique<WindowsFileWatcher>(); }
} // namespace semantic_fs::monitoring
