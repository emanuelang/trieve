#include "semantic_fs/monitoring/reconciliation_service.h"

namespace semantic_fs::monitoring {
namespace {
constexpr std::int64_t kFifteenMinutesMicroseconds = 15LL * 60 * 1'000'000;
bool same(const RelativePath& left, const RelativePath& right) { return left.components == right.components; }
bool covers(const RelativePath& prefix, const RelativePath& value) { return prefix.components.size() <= value.components.size() && std::equal(prefix.components.begin(),prefix.components.end(),value.components.begin()); }
}

ReconciliationAdmissionStatus ReconciliationScheduler::request(const RootId& root, GapEpoch epoch, ReconciliationTrigger trigger)
{
    const std::string key(root.value());
    const auto now = clock_.monotonicNow();
    if (const auto active = active_.find(key); active != active_.end()) {
        if (epoch > active->second.epoch) {
            active->second.epoch = epoch;
            lastAdmission_.insert_or_assign(key, now);
        }
        return ReconciliationAdmissionStatus::Coalesced;
    }

    if (trigger == ReconciliationTrigger::Interval) {
        const auto previous = lastAdmission_.find(key);
        if (previous != lastAdmission_.end() && (now.microsecondsSinceOrigin < previous->second.microsecondsSinceOrigin
            || now.microsecondsSinceOrigin - previous->second.microsecondsSinceOrigin < kFifteenMinutesMicroseconds)) {
            return ReconciliationAdmissionStatus::NotDue;
        }
    }

    active_.emplace(key, Run{epoch});
    lastAdmission_.insert_or_assign(key, now);
    return ReconciliationAdmissionStatus::Admitted;
}

bool ReconciliationScheduler::complete(const RootId& root, GapEpoch epoch)
{
    const auto found = active_.find(std::string(root.value()));
    if (found == active_.end() || found->second.epoch != epoch) return false;
    active_.erase(found);
    return true;
}

ReconciliationStepResult ReconciliationService::step(ICatalogOutboxWriter& writer, const RootId& root, GapEpoch epoch, const std::vector<ReconciliationCatalogEntry>& catalog, UtcTimestamp now)
{
    const auto read=writer.beginReconciliation(root,epoch,now);
    if((read.status!=ReconciliationStorageStatus::Started&&read.status!=ReconciliationStorageStatus::Active)||!read.run) return {ReconciliationStepStatus::Busy,0};
    root_.reset(); finalOutboxEvent_=read.run->finalOutboxEvent; runId_=read.run->runId; epoch_=0; requiredHighWater_=0; eligible_=false;

    std::size_t applied=0;
    const auto recorded = [&](const MutationResult& result) { if (result.eventId) finalOutboxEvent_=result.eventId; return result.status==MutationStatus::Applied||result.status==MutationStatus::Equivalent; };
    for(const auto& disk:read.staged) { const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto& item){return same(item.path.relative,disk.path.relative);}); if(!recorded(writer.applyReconciliation(runId_,{root,disk.path,disk.metadata,now,found==catalog.end()?ChangeKind::Discovered:ChangeKind::Modified,ObservationSource::Reconciliation,{}}))) return {ReconciliationStepStatus::Dirty,applied}; ++applied; }
    for(const auto& item:catalog) if(std::none_of(read.staged.begin(),read.staged.end(),[&](const auto& disk){return same(item.path.relative,disk.path.relative);})&&std::any_of(read.staged.begin(),read.staged.end(),[&](const auto& disk){return disk.completedSubtree&&covers(disk.path.relative,item.path.relative);})) { if(!recorded(writer.applyReconciliation(runId_,{root,item.path,item.metadata,now,ChangeKind::Removed,ObservationSource::Reconciliation,{}}))) return {ReconciliationStepStatus::Dirty,applied}; ++applied; }
    const auto cursor=read.run->finalizeCursor+read.staged.size(); const bool complete=read.run->scanArmed&&cursor==read.run->scanCursor; if(writer.advanceReconciliation(root,epoch,cursor,complete)!=ReconciliationStorageStatus::Stored) return {ReconciliationStepStatus::Dirty,applied}; if(!complete) return {ReconciliationStepStatus::Dirty,applied}; root_=root; epoch_=epoch; requiredHighWater_=read.run->requiredHighWater.value_or(0); return {ReconciliationStepStatus::AwaitingBarrier,applied};
}
ReconciliationStepResult ReconciliationService::stepPage(ICatalogOutboxWriter& writer, const RootId& root, GapEpoch epoch, const CatalogPage& catalog, UtcTimestamp now)
{
    if (catalog.status != ReconciliationStorageStatus::Stored || catalog.entries.size() > 256) return {ReconciliationStepStatus::Dirty, 0};
    return step(writer, root, epoch, catalog.entries, now);
}
bool ReconciliationService::accept(BarrierReached barrier) { if(!root_||barrier.epoch!=epoch_) { eligible_=false; return false; } eligible_=true; return true; }
bool ReconciliationService::accept(ICatalogOutboxWriter& writer, BarrierReached barrier)
{
    if (!root_ || barrier.epoch != epoch_ || barrier.highWater < requiredHighWater_) { eligible_ = false; return false; }
    eligible_ = writer.completeReconciliation({*root_, runId_, epoch_, barrier, finalOutboxEvent_, requiredHighWater_}) == ReconciliationStorageStatus::Stored;
    return eligible_;
}
} // namespace semantic_fs::monitoring
