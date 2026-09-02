#include "semantic_fs/monitoring/i_file_watcher.h"

#include <windows.h>

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
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

bool safeName(std::wstring_view name)
{
    if (name.empty() || name.starts_with(L'\\') || name.starts_with(L'/') || name.find(L'\0') != std::wstring_view::npos) return false;
    std::size_t start{};
    while (start < name.size()) {
        const auto end = name.find_first_of(L"\\/", start);
        if (name.substr(start, end - start) == L"..") return false;
        if (end == std::wstring_view::npos) break;
        start = end + 1;
    }
    return true;
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
        DWORD offset{};
        std::optional<WatcherEvent> old;
        while (offset < byteCount) {
            if (byteCount - offset < offsetof(FILE_NOTIFY_INFORMATION, FileName)) { markDirty(DirtyReason::DecodeGap); return; }
            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(bytes + offset);
            const auto recordSize = offsetof(FILE_NOTIFY_INFORMATION, FileName) + info->FileNameLength;
            if (info->FileNameLength % sizeof(wchar_t) || recordSize > byteCount - offset || !safeName({info->FileName, info->FileNameLength / sizeof(wchar_t)})) { markDirty(DirtyReason::DecodeGap); return; }
            auto name = utf8(info->FileName, static_cast<int>(info->FileNameLength / sizeof(wchar_t)));
            if (!name) { markDirty(DirtyReason::DecodeGap); return; }
            const AbsolutePath path{root.root.utf8 + "/" + *name};
            auto event = WatcherEvent{root.rootId, WatcherEventKind::Modified, path, {}};
            if (info->Action == FILE_ACTION_ADDED) event.kind = WatcherEventKind::Created;
            else if (info->Action == FILE_ACTION_REMOVED) event.kind = WatcherEventKind::Removed;
            else if (info->Action == FILE_ACTION_RENAMED_OLD_NAME) { if (old) enqueue(*old); old = WatcherEvent{root.rootId, WatcherEventKind::Removed, path, {}}; }
            else if (info->Action == FILE_ACTION_RENAMED_NEW_NAME && old) { event.kind = WatcherEventKind::Renamed; event.previousPath = old->path; enqueue(std::move(event)); old.reset(); }
            else { if (old) { enqueue(*old); old.reset(); } enqueue(std::move(event)); }
            if (!info->NextEntryOffset) { if (old) enqueue(*old); return; }
            if (info->NextEntryOffset < recordSize || info->NextEntryOffset > byteCount - offset) { markDirty(DirtyReason::DecodeGap); return; }
            offset += info->NextEntryOffset;
        }
        markDirty(DirtyReason::DecodeGap);
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

std::unique_ptr<IFileWatcher> makeWindowsFileWatcher() { return std::make_unique<WindowsFileWatcher>(); }
} // namespace semantic_fs::monitoring
