/*
 *  Copyright (c) 2021 NetEase Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * Project: Curve
 * Created Date: 2021-09-11
 * Author: Jingli Chen (Wine93)
 */

#include <list>

#include "curvefs/src/client/client_operator.h"

namespace curvefs {
namespace client {

using ::curvefs::metaserver::DentryFlag;
using ::curvefs::mds::topology::PartitionTxId;

#define LOG_ERROR(action, rc) \
    LOG(ERROR) << action << " failed, retCode = " << rc \
               << ", DebugString = " << DebugString();

RenameOperator::RenameOperator(uint32_t fsId,
                               uint64_t parentId,
                               std::string name,
                               uint64_t newParentId,
                               std::string newname,
                               std::shared_ptr<DentryCacheManager> dentryManager,  // NOLINT
                               std::shared_ptr<InodeCacheManager> inodeManager,
                               std::shared_ptr<MetaServerClient> metaClient,
                               std::shared_ptr<MdsClient> mdsClient)
    : fsId_(fsId),
      parentId_(parentId),
      name_(name),
      newParentId_(newParentId),
      newname_(newname),
      srcPartitionId_(0),
      dstPartitionId_(0),
      srcTxId_(0),
      dstTxId_(0),
      oldInodeId_(0),
      dentryManager_(dentryManager),
      inodeManager_(inodeManager),
      metaClient_(metaClient),
      mdsClient_(mdsClient) {}

std::string RenameOperator::DebugString() {
    std::ostringstream os;
    os << "( fsId = " << fsId_
       << ", parentId = " << parentId_ << ", name = " << name_
       << ", newParentId = " << newParentId_ << ", newname = " << newname_
       << ", srcPartitionId = " << srcPartitionId_
       << ", dstPartitionId = " << dstPartitionId_
       << ", srcTxId = " << srcTxId_ << ", dstTxId_ = " << dstTxId_
       << ", oldInodeId = " << oldInodeId_
       << ", srcDentry = [" << srcDentry_.ShortDebugString() << "]"
       << ", dstDentry = [" << dstDentry_.ShortDebugString() << "]"
       << ", prepare dentry = [" << dentry_.ShortDebugString() << "]"
       << ", prepare new dentry = [" << newDentry_.ShortDebugString() << "]"
       << " )";
    return os.str();
}

CURVEFS_ERROR RenameOperator::GetTxId(uint32_t fsId,
                                      uint64_t inodeId,
                                      uint32_t* partitionId,
                                      uint64_t* txId) {
    auto rc = metaClient_->GetTxId(fsId, inodeId, partitionId, txId);
    if (rc != MetaStatusCode::OK) {
        LOG_ERROR("GetTxId", rc);
    }
    return MetaStatusCodeToCurvefsErrCode(rc);
}

void RenameOperator::SetTxId(uint32_t partitionId, uint64_t txId) {
    metaClient_->SetTxId(partitionId, txId);
}

CURVEFS_ERROR RenameOperator::GetTxId() {
    auto rc = GetTxId(fsId_, parentId_, &srcPartitionId_, &srcTxId_);
    if (rc != CURVEFS_ERROR::OK) {
        LOG_ERROR("GetTxId", rc);
        return rc;
    }

    rc = GetTxId(fsId_, newParentId_, &dstPartitionId_, &dstTxId_);
    if (rc != CURVEFS_ERROR::OK) {
        LOG_ERROR("GetTxId", rc);
    }

    return rc;
}

// TODO(Wine93): we should improve the check for whether a directory is empty
CURVEFS_ERROR RenameOperator::CheckOverwrite() {
    if (dstDentry_.flag() & DentryFlag::TYPE_FILE_FLAG) {
        return CURVEFS_ERROR::OK;
    }

    std::list<Dentry> dentrys;
    auto rc = dentryManager_->ListDentry(dstDentry_.inodeid(), &dentrys, 1);
    if (rc == CURVEFS_ERROR::OK && !dentrys.empty()) {
        LOG(ERROR) << "The directory is not empty"
                   << ", dentry = (" << dstDentry_.ShortDebugString() << ")";
        rc = CURVEFS_ERROR::NOTEMPTY;
    }

    return rc;
}

// The rename operate must met the following 2 conditions:
//   (1) the source dentry must exist
//   (2) if the target dentry exist then it must be file or an empty directory
CURVEFS_ERROR RenameOperator::Precheck() {
    auto rc = dentryManager_->GetDentry(parentId_, name_, &srcDentry_);
    if (rc != CURVEFS_ERROR::OK) {
        LOG_ERROR("GetDentry", rc);
        return rc;
    }

    rc = dentryManager_->GetDentry(newParentId_, newname_, &dstDentry_);
    if (rc == CURVEFS_ERROR::NOTEXIST) {
        return CURVEFS_ERROR::OK;
    } else if (rc == CURVEFS_ERROR::OK) {
        oldInodeId_ = dstDentry_.inodeid();
        return CheckOverwrite();
    }

    LOG_ERROR("GetDentry", rc);
    return rc;
}

CURVEFS_ERROR RenameOperator::PrepareRenameTx(
    const std::vector<Dentry>& dentrys) {
    auto rc = metaClient_->PrepareRenameTx(dentrys);
    if (rc != MetaStatusCode::OK) {
        LOG_ERROR("PrepareRenameTx", rc);
    }

    return MetaStatusCodeToCurvefsErrCode(rc);
}

CURVEFS_ERROR RenameOperator::PrepareTx() {
    dentry_ = Dentry(srcDentry_);
    dentry_.set_txid(srcTxId_ + 1);
    dentry_.set_flag(dentry_.flag() |
                     DentryFlag::DELETE_MARK_FLAG |
                     DentryFlag::TRANSACTION_PREPARE_FLAG);

    newDentry_ = Dentry(srcDentry_);
    newDentry_.set_parentinodeid(newParentId_);
    newDentry_.set_name(newname_);
    newDentry_.set_txid(dstTxId_ + 1);
    newDentry_.set_flag(newDentry_.flag() |
                        DentryFlag::TRANSACTION_PREPARE_FLAG);

    CURVEFS_ERROR rc;
    std::vector<Dentry> dentrys{ dentry_ };
    if (srcPartitionId_ == dstPartitionId_) {
        dentrys.push_back(newDentry_);
        rc = PrepareRenameTx(dentrys);
    } else {
        rc = PrepareRenameTx(dentrys);
        if (rc == CURVEFS_ERROR::OK) {
            dentrys[0] = newDentry_;
            rc = PrepareRenameTx(dentrys);
        }
    }

    if (rc != CURVEFS_ERROR::OK) {
        LOG_ERROR("PrepareTx", rc);
    }
    return rc;
}

CURVEFS_ERROR RenameOperator::CommitTx() {
    PartitionTxId partitionTxId;
    std::vector<PartitionTxId> txIds;

    partitionTxId.set_partitionid(srcPartitionId_);
    partitionTxId.set_txid(srcTxId_ + 1);
    txIds.push_back(partitionTxId);

    if (srcPartitionId_ != dstPartitionId_) {
        partitionTxId.set_partitionid(dstPartitionId_);
        partitionTxId.set_txid(dstTxId_ + 1);
        txIds.push_back(partitionTxId);
    }

    auto rc = mdsClient_->CommitTx(txIds);
    if (rc != TopoStatusCode::TOPO_OK) {
        LOG_ERROR("CommitTx", rc);
        return CURVEFS_ERROR::INTERNAL;
    }
    return CURVEFS_ERROR::OK;
}

void RenameOperator::UnlinkOldInode() {
    if (oldInodeId_ == 0) {
        return;
    }

    std::shared_ptr<InodeWrapper> inodeWrapper;
    auto rc = inodeManager_->GetInode(oldInodeId_, inodeWrapper);
    if (rc != CURVEFS_ERROR::OK) {
        LOG_ERROR("GetInode", rc);
        return;
    }

    rc = inodeWrapper->UnLinkLocked();
    if (rc != CURVEFS_ERROR::OK) {
        LOG_ERROR("UnLink", rc);
    }
}

void RenameOperator::UpdateCache() {
    dentryManager_->DeleteCache(parentId_, name_);
    dentryManager_->InsertOrReplaceCache(newDentry_);
    SetTxId(srcPartitionId_, srcTxId_ + 1);
    SetTxId(dstPartitionId_, dstTxId_ + 1);
}

}  // namespace client
}  // namespace curvefs
