#include "semantic_fs/monitoring/reconciliation_service.h"

namespace semantic_fs::monitoring {
namespace {
bool same(const RelativePath& left, const RelativePath& right) { return left.components == right.components; }
bool covers(const RelativePath& prefix, const RelativePath& value) { return prefix.components.size() <= value.components.size() && std::equal(prefix.components.begin(),prefix.components.end(),value.components.begin()); }
}
ReconciliationStepResult ReconciliationService::step(ICatalogOutboxWriter& writer, const RootId& root, GapEpoch epoch, const std::vector<ReconciliationCatalogEntry>& catalog, UtcTimestamp now)
{
    const auto read=writer.beginReconciliation(root,epoch,now);
    if((read.status!=ReconciliationStorageStatus::Started&&read.status!=ReconciliationStorageStatus::Active)||!read.run) return {ReconciliationStepStatus::Busy,0};
    std::size_t applied=0;
    for(const auto& disk:read.staged) { const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto& item){return same(item.path.relative,disk.path.relative);}); const auto result=writer.apply({root,disk.path,disk.metadata,now,found==catalog.end()?ChangeKind::Discovered:ChangeKind::Modified,ObservationSource::Reconciliation,{}}); if(result.status!=MutationStatus::Applied&&result.status!=MutationStatus::Equivalent) return {ReconciliationStepStatus::Dirty,applied}; ++applied; }
    for(const auto& item:catalog) if(std::none_of(read.staged.begin(),read.staged.end(),[&](const auto& disk){return same(item.path.relative,disk.path.relative);})&&std::any_of(read.staged.begin(),read.staged.end(),[&](const auto& disk){return disk.completedSubtree&&covers(disk.path.relative,item.path.relative);})) { const auto result=writer.apply({root,item.path,item.metadata,now,ChangeKind::Removed,ObservationSource::Reconciliation,{}}); if(result.status!=MutationStatus::Applied&&result.status!=MutationStatus::Equivalent) return {ReconciliationStepStatus::Dirty,applied}; ++applied; }
    const auto cursor=read.run->finalizeCursor+read.staged.size(); const bool complete=cursor==read.run->scanCursor; if(writer.advanceReconciliation(root,epoch,cursor,complete)!=ReconciliationStorageStatus::Stored) return {ReconciliationStepStatus::Dirty,applied}; root_=root; epoch_=epoch; eligible_=false; return {ReconciliationStepStatus::AwaitingBarrier,applied};
}
bool ReconciliationService::accept(BarrierReached barrier) { if(!root_||barrier.epoch!=epoch_) { eligible_=false; return false; } eligible_=true; return true; }
} // namespace semantic_fs::monitoring
