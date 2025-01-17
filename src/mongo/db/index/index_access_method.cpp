/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kIndex

#include "mongo/platform/basic.h"

#include "mongo/db/index/btree_access_method.h"

#include <utility>
#include <vector>

#include "mongo/base/error_codes.h"
#include "mongo/base/status.h"
#include "mongo/db/catalog/index_catalog.h"
#include "mongo/db/catalog/index_consistency.h"
#include "mongo/db/client.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/curop.h"
#include "mongo/db/index/index_build_interceptor.h"
#include "mongo/db/index/index_descriptor.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/keypattern.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/repl/timestamp_block.h"
#include "mongo/db/storage/durable_catalog.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/logv2/log.h"
#include "mongo/util/progress_meter.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/stacktrace.h"

namespace mongo {

using std::endl;
using std::pair;
using std::set;
using std::vector;

using IndexVersion = IndexDescriptor::IndexVersion;

MONGO_FAIL_POINT_DEFINE(hangDuringIndexBuildBulkLoadYield);

namespace {

// Reserved RecordId against which multikey metadata keys are indexed.
static const RecordId kMultikeyMetadataKeyId =
    RecordId{RecordId::ReservedId::kWildcardMultikeyMetadataId};

/**
 * Returns true if at least one prefix of any of the indexed fields causes the index to be
 * multikey, and returns false otherwise. This function returns false if the 'multikeyPaths'
 * vector is empty.
 */
bool isMultikeyFromPaths(const MultikeyPaths& multikeyPaths) {
    return std::any_of(multikeyPaths.cbegin(),
                       multikeyPaths.cend(),
                       [](const std::set<std::size_t>& components) { return !components.empty(); });
}
}  // namespace

struct BtreeExternalSortComparison {
    typedef std::pair<KeyString::Value, mongo::NullValue> Data;
    int operator()(const Data& l, const Data& r) const {
        return l.first.compare(r.first);
    }
};

AbstractIndexAccessMethod::AbstractIndexAccessMethod(IndexCatalogEntry* btreeState,
                                                     std::unique_ptr<SortedDataInterface> btree)
    : _indexCatalogEntry(btreeState),
      _descriptor(btreeState->descriptor()),
      _newInterface(std::move(btree)) {
    verify(IndexDescriptor::isIndexVersionSupported(_descriptor->version()));
}

bool AbstractIndexAccessMethod::isFatalError(OperationContext* opCtx,
                                             StatusWith<bool> status,
                                             KeyString::Value key) {
    // If the status is Status::OK() return false immediately.
    if (status.isOK()) {
        return false;
    }

    // A document might be indexed multiple times during a background index build if it moves ahead
    // of the cursor (e.g. via an update). We test this scenario and swallow the error accordingly.
    if (status == ErrorCodes::DuplicateKeyValue && !_indexCatalogEntry->isReady(opCtx)) {
        LOGV2_DEBUG(20681,
                    3,
                    "KeyString {key} already in index during background indexing (ok)",
                    "key"_attr = key);
        return false;
    }
    return true;
}

// Find the keys for obj, put them in the tree pointing to loc.
Status AbstractIndexAccessMethod::insert(OperationContext* opCtx,
                                         const BSONObj& obj,
                                         const RecordId& loc,
                                         const InsertDeleteOptions& options,
                                         KeyHandlerFn&& onDuplicateKey,
                                         int64_t* numInserted) {
    invariant(options.fromIndexBuilder || !_indexCatalogEntry->isHybridBuilding());

    KeyStringSet multikeyMetadataKeys;
    KeyStringSet keys;
    MultikeyPaths multikeyPaths;

    getKeys(obj,
            options.getKeysMode,
            GetKeysContext::kAddingKeys,
            &keys,
            &multikeyMetadataKeys,
            &multikeyPaths,
            loc,
            kNoopOnSuppressedErrorFn);

    return insertKeysAndUpdateMultikeyPaths(opCtx,
                                            keys,
                                            multikeyMetadataKeys,
                                            multikeyPaths,
                                            loc,
                                            options,
                                            std::move(onDuplicateKey),
                                            numInserted);
}

Status AbstractIndexAccessMethod::insertKeysAndUpdateMultikeyPaths(
    OperationContext* opCtx,
    const KeyStringSet& keys,
    const KeyStringSet& multikeyMetadataKeys,
    const MultikeyPaths& multikeyPaths,
    const RecordId& loc,
    const InsertDeleteOptions& options,
    KeyHandlerFn&& onDuplicateKey,
    int64_t* numInserted) {
    // Insert the specified data keys into the index.
    auto status = insertKeys(opCtx, keys, loc, options, std::move(onDuplicateKey), numInserted);
    if (!status.isOK()) {
        return status;
    }
    // If these keys should cause the index to become multikey, pass them into the catalog.
    if (shouldMarkIndexAsMultikey(keys.size(), multikeyMetadataKeys, multikeyPaths)) {
        _indexCatalogEntry->setMultikey(opCtx, multikeyMetadataKeys, multikeyPaths);
    }
    // If we have some multikey metadata keys, they should have been added while marking the index
    // as multikey in the catalog. Add them to the count of keys inserted for completeness.
    if (numInserted && !multikeyMetadataKeys.empty()) {
        *numInserted += multikeyMetadataKeys.size();
    }
    return Status::OK();
}

Status AbstractIndexAccessMethod::insertKeys(OperationContext* opCtx,
                                             const KeyStringSet& keys,
                                             const RecordId& loc,
                                             const InsertDeleteOptions& options,
                                             KeyHandlerFn&& onDuplicateKey,
                                             int64_t* numInserted) {
    // Initialize the 'numInserted' out-parameter to zero in case the caller did not already do so.
    if (numInserted) {
        *numInserted = 0;
    }
    // Add all new keys into the index. The RecordId for each is already encoded in the KeyString.
    for (const auto& keyString : keys) {
        bool unique = _descriptor->unique();
        auto result = _newInterface->insert(opCtx, keyString, !unique /* dupsAllowed */);

        // When duplicates are encountered and allowed, retry with dupsAllowed. Call
        // onDuplicateKey() with the inserted duplicate key.
        if (ErrorCodes::DuplicateKey == result.getStatus().code() && options.dupsAllowed) {
            invariant(unique);

            result = _newInterface->insert(opCtx, keyString, true /* dupsAllowed */);
            if (!result.isOK()) {
                return result.getStatus();
            } else if (result.getValue() && onDuplicateKey) {
                // Only run the duplicate key handler if we inserted the key ourselves. Someone else
                // could have already inserted this exact key, but in that case we don't count it as
                // a duplicate.
                auto status = onDuplicateKey(keyString);
                if (!status.isOK()) {
                    return status;
                }
            }
        } else if (!result.isOK()) {
            return result.getStatus();
        }
        if (isFatalError(opCtx, result, keyString)) {
            return result.getStatus();
        }
    }
    if (numInserted) {
        *numInserted = keys.size();
    }
    return Status::OK();
}

void AbstractIndexAccessMethod::removeOneKey(OperationContext* opCtx,
                                             const KeyString::Value& keyString,
                                             const RecordId& loc,
                                             bool dupsAllowed) {

    try {
        _newInterface->unindex(opCtx, keyString, dupsAllowed);
    } catch (AssertionException& e) {
        LOGV2(20683,
              "Assertion failure: _unindex failed on: {descriptorParentNamespace} for index: "
              "{descriptorIndexName}. {error}  KeyString:{keyString}  dl:{recordId}",
              "Assertion failure: _unindex failed",
              "error"_attr = redact(e),
              "keyString"_attr = keyString,
              "recordId"_attr = loc,
              "descriptorParentNamespace"_attr = _descriptor->parentNS(),
              "descriptorIndexName"_attr = _descriptor->indexName());
        printStackTrace();
    }
}

std::unique_ptr<SortedDataInterface::Cursor> AbstractIndexAccessMethod::newCursor(
    OperationContext* opCtx, bool isForward) const {
    return _newInterface->newCursor(opCtx, isForward);
}

std::unique_ptr<SortedDataInterface::Cursor> AbstractIndexAccessMethod::newCursor(
    OperationContext* opCtx) const {
    return newCursor(opCtx, true);
}

Status AbstractIndexAccessMethod::removeKeys(OperationContext* opCtx,
                                             const KeyStringSet& keys,
                                             const RecordId& loc,
                                             const InsertDeleteOptions& options,
                                             int64_t* numDeleted) {

    for (const auto& key : keys) {
        removeOneKey(opCtx, key, loc, options.dupsAllowed);
    }

    *numDeleted = keys.size();
    return Status::OK();
}

Status AbstractIndexAccessMethod::initializeAsEmpty(OperationContext* opCtx) {
    return _newInterface->initAsEmpty(opCtx);
}

RecordId AbstractIndexAccessMethod::findSingle(OperationContext* opCtx,
                                               const BSONObj& requestedKey) const {
    // Generate the key for this index.
    KeyString::Value actualKey = [&]() {
        if (_indexCatalogEntry->getCollator()) {
            // For performance, call get keys only if there is a non-simple collation.
            KeyStringSet keys;
            KeyStringSet* multikeyMetadataKeys = nullptr;
            MultikeyPaths* multikeyPaths = nullptr;
            getKeys(requestedKey,
                    GetKeysMode::kEnforceConstraints,
                    GetKeysContext::kAddingKeys,
                    &keys,
                    multikeyMetadataKeys,
                    multikeyPaths,
                    boost::none,  // loc
                    kNoopOnSuppressedErrorFn);
            invariant(keys.size() == 1);
            return *keys.begin();
        } else {
            KeyString::HeapBuilder requestedKeyString(
                getSortedDataInterface()->getKeyStringVersion(),
                BSONObj::stripFieldNames(requestedKey),
                getSortedDataInterface()->getOrdering());
            return requestedKeyString.release();
        }
    }();

    std::unique_ptr<SortedDataInterface::Cursor> cursor(_newInterface->newCursor(opCtx));
    const auto requestedInfo = kDebugBuild ? SortedDataInterface::Cursor::kKeyAndLoc
                                           : SortedDataInterface::Cursor::kWantLoc;
    if (auto kv = cursor->seekExact(actualKey, requestedInfo)) {
        // StorageEngine should guarantee these.
        dassert(!kv->loc.isNull());
        dassert(kv->key.woCompare(KeyString::toBson(actualKey.getBuffer(),
                                                    actualKey.getSize(),
                                                    getSortedDataInterface()->getOrdering(),
                                                    actualKey.getTypeBits()),
                                  /*order*/ BSONObj(),
                                  /*considerFieldNames*/ false) == 0);

        return kv->loc;
    }

    return RecordId();
}

void AbstractIndexAccessMethod::validate(OperationContext* opCtx,
                                         int64_t* numKeys,
                                         ValidateResults* fullResults) const {
    long long keys = 0;
    _newInterface->fullValidate(opCtx, &keys, fullResults);
    *numKeys = keys;
}

bool AbstractIndexAccessMethod::appendCustomStats(OperationContext* opCtx,
                                                  BSONObjBuilder* output,
                                                  double scale) const {
    return _newInterface->appendCustomStats(opCtx, output, scale);
}

long long AbstractIndexAccessMethod::getSpaceUsedBytes(OperationContext* opCtx) const {
    return _newInterface->getSpaceUsedBytes(opCtx);
}

pair<KeyStringSet, KeyStringSet> AbstractIndexAccessMethod::setDifference(const KeyStringSet& left,
                                                                          const KeyStringSet& right,
                                                                          Ordering ordering) {
    // Two iterators to traverse the two sets in sorted order.
    auto leftIt = left.begin();
    auto rightIt = right.begin();
    KeyStringSet onlyLeft;
    KeyStringSet onlyRight;

    while (leftIt != left.end() && rightIt != right.end()) {
        const int cmp = leftIt->compare(*rightIt);
        if (cmp == 0) {
            /*
             * 'leftIt' and 'rightIt' compare equal using compare(), but may not be identical, which
             * should result in an index change.
             */
            auto leftKey = KeyString::toBson(
                leftIt->getBuffer(), leftIt->getSize(), ordering, leftIt->getTypeBits());
            auto rightKey = KeyString::toBson(
                rightIt->getBuffer(), rightIt->getSize(), ordering, rightIt->getTypeBits());
            if (!leftKey.binaryEqual(rightKey)) {
                onlyLeft.insert(*leftIt);
                onlyRight.insert(*rightIt);
            }
            ++leftIt;
            ++rightIt;
            continue;
        } else if (cmp > 0) {
            onlyRight.insert(*rightIt);
            ++rightIt;
        } else {
            onlyLeft.insert(*leftIt);
            ++leftIt;
        }
    }

    // Add the rest of 'left' to 'onlyLeft', and the rest of 'right' to 'onlyRight', if any.
    onlyLeft.insert(leftIt, left.end());
    onlyRight.insert(rightIt, right.end());

    return {std::move(onlyLeft), std::move(onlyRight)};
}

void AbstractIndexAccessMethod::prepareUpdate(OperationContext* opCtx,
                                              IndexCatalogEntry* index,
                                              const BSONObj& from,
                                              const BSONObj& to,
                                              const RecordId& record,
                                              const InsertDeleteOptions& options,
                                              UpdateTicket* ticket) const {
    const MatchExpression* indexFilter = index->getFilterExpression();
    if (!indexFilter || indexFilter->matchesBSON(from)) {
        // Override key constraints when generating keys for removal. This only applies to keys
        // that do not apply to a partial filter expression.
        const auto getKeysMode = index->isHybridBuilding()
            ? IndexAccessMethod::GetKeysMode::kRelaxConstraintsUnfiltered
            : options.getKeysMode;

        // There's no need to compute the prefixes of the indexed fields that possibly caused the
        // index to be multikey when the old version of the document was written since the index
        // metadata isn't updated when keys are deleted.
        getKeys(from,
                getKeysMode,
                GetKeysContext::kRemovingKeys,
                &ticket->oldKeys,
                nullptr,
                nullptr,
                record,
                kNoopOnSuppressedErrorFn);
    }

    if (!indexFilter || indexFilter->matchesBSON(to)) {
        getKeys(to,
                options.getKeysMode,
                GetKeysContext::kAddingKeys,
                &ticket->newKeys,
                &ticket->newMultikeyMetadataKeys,
                &ticket->newMultikeyPaths,
                record,
                kNoopOnSuppressedErrorFn);
    }

    ticket->loc = record;
    ticket->dupsAllowed = options.dupsAllowed;

    std::tie(ticket->removed, ticket->added) =
        setDifference(ticket->oldKeys, ticket->newKeys, getSortedDataInterface()->getOrdering());

    ticket->_isValid = true;
}

Status AbstractIndexAccessMethod::update(OperationContext* opCtx,
                                         const UpdateTicket& ticket,
                                         int64_t* numInserted,
                                         int64_t* numDeleted) {
    invariant(!_indexCatalogEntry->isHybridBuilding());
    invariant(ticket.newKeys.size() ==
              ticket.oldKeys.size() + ticket.added.size() - ticket.removed.size());
    invariant(numInserted);
    invariant(numDeleted);

    *numInserted = 0;
    *numDeleted = 0;

    if (!ticket._isValid) {
        return Status(ErrorCodes::InternalError, "Invalid UpdateTicket in update");
    }

    for (const auto& remKey : ticket.removed) {
        _newInterface->unindex(opCtx, remKey, ticket.dupsAllowed);
    }

    // Add all new data keys into the index.
    for (const auto keyString : ticket.added) {
        auto result = _newInterface->insert(opCtx, keyString, ticket.dupsAllowed);
        if (isFatalError(opCtx, result, keyString)) {
            return result.getStatus();
        }
    }

    // If these keys should cause the index to become multikey, pass them into the catalog.
    if (shouldMarkIndexAsMultikey(
            ticket.newKeys.size(), ticket.newMultikeyMetadataKeys, ticket.newMultikeyPaths)) {
        _indexCatalogEntry->setMultikey(
            opCtx, ticket.newMultikeyMetadataKeys, ticket.newMultikeyPaths);
    }

    // If we have some multikey metadata keys, they should have been added while marking the index
    // as multikey in the catalog. Add them to the count of keys inserted for completeness.
    *numInserted = ticket.added.size() + ticket.newMultikeyMetadataKeys.size();
    *numDeleted = ticket.removed.size();

    return Status::OK();
}

Status AbstractIndexAccessMethod::compact(OperationContext* opCtx) {
    return this->_newInterface->compact(opCtx);
}

class AbstractIndexAccessMethod::BulkBuilderImpl : public IndexAccessMethod::BulkBuilder {
public:
    BulkBuilderImpl(IndexCatalogEntry* indexCatalogEntry,
                    const IndexDescriptor* descriptor,
                    size_t maxMemoryUsageBytes);

    Status insert(OperationContext* opCtx,
                  const BSONObj& obj,
                  const RecordId& loc,
                  const InsertDeleteOptions& options) final;

    const MultikeyPaths& getMultikeyPaths() const final;

    bool isMultikey() const final;

    /**
     * Inserts all multikey metadata keys cached during the BulkBuilder's lifetime into the
     * underlying Sorter, finalizes it, and returns an iterator over the sorted dataset.
     */
    Sorter::Iterator* done() final;

    int64_t getKeysInserted() const final;

private:
    std::unique_ptr<Sorter> _sorter;
    IndexCatalogEntry* _indexCatalogEntry;
    int64_t _keysInserted = 0;

    // Set to true if any document added to the BulkBuilder causes the index to become multikey.
    bool _isMultiKey = false;

    // Holds the path components that cause this index to be multikey. The '_indexMultikeyPaths'
    // vector remains empty if this index doesn't support path-level multikey tracking.
    MultikeyPaths _indexMultikeyPaths;

    // Caches the set of all multikey metadata keys generated during the bulk build process.
    // These are inserted into the sorter after all normal data keys have been added, just
    // before the bulk build is committed.
    KeyStringSet _multikeyMetadataKeys;
};

std::unique_ptr<IndexAccessMethod::BulkBuilder> AbstractIndexAccessMethod::initiateBulk(
    size_t maxMemoryUsageBytes) {
    return std::make_unique<BulkBuilderImpl>(_indexCatalogEntry, _descriptor, maxMemoryUsageBytes);
}

AbstractIndexAccessMethod::BulkBuilderImpl::BulkBuilderImpl(IndexCatalogEntry* index,
                                                            const IndexDescriptor* descriptor,
                                                            size_t maxMemoryUsageBytes)
    : _sorter(Sorter::make(
          SortOptions()
              .TempDir(storageGlobalParams.dbpath + "/_tmp")
              .ExtSortAllowed()
              .MaxMemoryUsageBytes(maxMemoryUsageBytes),
          BtreeExternalSortComparison(),
          std::pair<KeyString::Value::SorterDeserializeSettings,
                    mongo::NullValue::SorterDeserializeSettings>(
              {index->accessMethod()->getSortedDataInterface()->getKeyStringVersion()}, {}))),
      _indexCatalogEntry(index) {}

Status AbstractIndexAccessMethod::BulkBuilderImpl::insert(OperationContext* opCtx,
                                                          const BSONObj& obj,
                                                          const RecordId& loc,
                                                          const InsertDeleteOptions& options) {
    KeyStringSet keys;
    MultikeyPaths multikeyPaths;

    try {
        _indexCatalogEntry->accessMethod()->getKeys(
            obj,
            options.getKeysMode,
            GetKeysContext::kAddingKeys,
            &keys,
            &_multikeyMetadataKeys,
            &multikeyPaths,
            loc,
            [&](Status status, const BSONObj&, boost::optional<RecordId>) {
                // If a key generation error was suppressed, record the document as "skipped" so the
                // index builder can retry at a point when data is consistent.
                auto interceptor = _indexCatalogEntry->indexBuildInterceptor();
                if (interceptor && interceptor->getSkippedRecordTracker()) {
                    LOGV2_DEBUG(20684,
                                1,
                                "Recording suppressed key generation error to retry later: "
                                "{error} on {loc}: {obj}",
                                "error"_attr = status,
                                "loc"_attr = loc,
                                "obj"_attr = redact(obj));
                    interceptor->getSkippedRecordTracker()->record(opCtx, loc);
                }
            });
    } catch (...) {
        return exceptionToStatus();
    }

    if (!multikeyPaths.empty()) {
        if (_indexMultikeyPaths.empty()) {
            _indexMultikeyPaths = multikeyPaths;
        } else {
            invariant(_indexMultikeyPaths.size() == multikeyPaths.size());
            for (size_t i = 0; i < multikeyPaths.size(); ++i) {
                _indexMultikeyPaths[i].insert(multikeyPaths[i].begin(), multikeyPaths[i].end());
            }
        }
    }

    for (const auto& keyString : keys) {
        _sorter->add(keyString, mongo::NullValue());
        ++_keysInserted;
    }

    _isMultiKey = _isMultiKey ||
        _indexCatalogEntry->accessMethod()->shouldMarkIndexAsMultikey(
            keys.size(), _multikeyMetadataKeys, multikeyPaths);

    return Status::OK();
}

const MultikeyPaths& AbstractIndexAccessMethod::BulkBuilderImpl::getMultikeyPaths() const {
    return _indexMultikeyPaths;
}

bool AbstractIndexAccessMethod::BulkBuilderImpl::isMultikey() const {
    return _isMultiKey;
}

IndexAccessMethod::BulkBuilder::Sorter::Iterator*
AbstractIndexAccessMethod::BulkBuilderImpl::done() {
    for (const auto& keyString : _multikeyMetadataKeys) {
        _sorter->add(keyString, mongo::NullValue());
        ++_keysInserted;
    }
    return _sorter->done();
}

int64_t AbstractIndexAccessMethod::BulkBuilderImpl::getKeysInserted() const {
    return _keysInserted;
}

void AbstractIndexAccessMethod::_yieldBulkLoad(OperationContext* opCtx,
                                               const NamespaceString& ns) const {
    // Releasing locks means a new snapshot should be acquired when restored.
    opCtx->recoveryUnit()->abandonSnapshot();

    auto locker = opCtx->lockState();
    Locker::LockSnapshot snapshot;
    if (locker->saveLockStateAndUnlock(&snapshot)) {

        // Track the number of yields in CurOp.
        CurOp::get(opCtx)->yielded();

        hangDuringIndexBuildBulkLoadYield.executeIf(
            [&](auto&&) {
                LOGV2(5180600, "Hanging index build during bulk load yield");
                hangDuringIndexBuildBulkLoadYield.pauseWhileSet();
            },
            [&](auto&& config) { return config.getStringField("namespace") == ns.ns(); });

        locker->restoreLockState(opCtx, snapshot);
    }
}

Status AbstractIndexAccessMethod::commitBulk(OperationContext* opCtx,
                                             BulkBuilder* bulk,
                                             bool dupsAllowed,
                                             int32_t yieldIterations,
                                             const KeyHandlerFn& onDuplicateKeyInserted,
                                             const RecordIdHandlerFn& onDuplicateRecord) {
    Timer timer;

    auto ns = _indexCatalogEntry->ns();

    std::unique_ptr<BulkBuilder::Sorter::Iterator> it(bulk->done());

    static constexpr char message[] = "Index Build: inserting keys from external sorter into index";
    ProgressMeterHolder pm;
    {
        stdx::unique_lock<Client> lk(*opCtx->getClient());
        pm.set(CurOp::get(opCtx)->setProgress_inlock(
            message, bulk->getKeysInserted(), 3 /* secondsBetween */));
    }

    auto builder = std::unique_ptr<SortedDataBuilderInterface>(
        _newInterface->getBulkBuilder(opCtx, dupsAllowed));

    KeyString::Value previousKey;

    for (int64_t i = 1; it->more(); ++i) {
        opCtx->checkForInterrupt();

        // Get the next datum and add it to the builder.
        BulkBuilder::Sorter::Data data = it->next();

        // Assert that keys are retrieved from the sorter in non-decreasing order, but only in debug
        // builds since this check can be expensive.
        int cmpData;
        if (kDebugBuild || _descriptor->unique()) {
            cmpData = data.first.compareWithoutRecordId(previousKey);
            if (cmpData < 0) {
                LOGV2_FATAL_NOTRACE(
                    31171,
                    "expected the next key{data_first} to be greater than or equal to the "
                    "previous key{previousKey}",
                    "data_first"_attr = data.first.toString(),
                    "previousKey"_attr = previousKey.toString());
            }
        }

        // Before attempting to insert, perform a duplicate key check.
        bool isDup = (_descriptor->unique()) ? (cmpData == 0) : false;
        if (isDup && !dupsAllowed) {
            Status status = _handleDuplicateKey(opCtx, data.first, onDuplicateRecord);
            if (!status.isOK()) {
                return status;
            }
            continue;
        }

        WriteUnitOfWork wunit(opCtx);
        Status status = builder->addKey(data.first);
        wunit.commit();

        if (!status.isOK()) {
            // Duplicates are checked before inserting.
            invariant(status.code() != ErrorCodes::DuplicateKey);
            return status;
        }

        previousKey = data.first;

        if (isDup) {
            status = onDuplicateKeyInserted(data.first);
            if (!status.isOK())
                return status;
        }

        // Starts yielding locks after the first non-zero 'yieldIterations' inserts.
        if (yieldIterations && i % yieldIterations == 0) {
            _yieldBulkLoad(opCtx, ns);
        }

        // If we're here either it's a dup and we're cool with it or the addKey went just fine.
        pm.hit();
    }

    pm.finished();

    LOGV2(20685,
          "Index build: inserted {bulk_getKeysInserted} keys from external sorter into index in "
          "{timer_seconds} seconds",
          "Index build: inserted keys from external sorter into index",
          "namespace"_attr = _descriptor->parentNS(),
          "index"_attr = _descriptor->indexName(),
          "keysInserted"_attr = bulk->getKeysInserted(),
          "duration"_attr = Milliseconds(Seconds(timer.seconds())));

    WriteUnitOfWork wunit(opCtx);
    builder->commit(true);
    wunit.commit();
    return Status::OK();
}

void AbstractIndexAccessMethod::setIndexIsMultikey(OperationContext* opCtx,
                                                   KeyStringSet multikeyMetadataKeys,
                                                   MultikeyPaths paths) {
    _indexCatalogEntry->setMultikey(opCtx, multikeyMetadataKeys, paths);
}

IndexAccessMethod::OnSuppressedErrorFn IndexAccessMethod::kNoopOnSuppressedErrorFn =
    [](Status status, const BSONObj& obj, boost::optional<RecordId> loc) {
        LOGV2_DEBUG(
            20686,
            1,
            "Suppressed key generation error: {error} when getting index keys for {loc}: {obj}",
            "error"_attr = redact(status),
            "loc"_attr = loc,
            "obj"_attr = redact(obj));
    };

void AbstractIndexAccessMethod::getKeys(const BSONObj& obj,
                                        GetKeysMode mode,
                                        GetKeysContext context,
                                        KeyStringSet* keys,
                                        KeyStringSet* multikeyMetadataKeys,
                                        MultikeyPaths* multikeyPaths,
                                        boost::optional<RecordId> id,
                                        OnSuppressedErrorFn onSuppressedError) const {
    static stdx::unordered_set<int> whiteList{ErrorCodes::CannotBuildIndexKeys,
                                              // Btree
                                              ErrorCodes::CannotIndexParallelArrays,
                                              // FTS
                                              16732,
                                              16733,
                                              16675,
                                              17261,
                                              17262,
                                              // Hash
                                              16766,
                                              // Ambiguous array field name
                                              16746,
                                              // Haystack
                                              16775,
                                              16776,
                                              // 2dsphere geo
                                              16755,
                                              16756,
                                              // 2d geo
                                              16804,
                                              13067,
                                              13068,
                                              13026,
                                              13027};
    try {
        doGetKeys(obj, context, keys, multikeyMetadataKeys, multikeyPaths, id);
    } catch (const AssertionException& ex) {
        // Suppress all indexing errors when mode is kRelaxConstraints.
        if (mode == GetKeysMode::kEnforceConstraints) {
            throw;
        }

        keys->clear();
        if (multikeyPaths) {
            multikeyPaths->clear();
        }
        // Only suppress the errors in the whitelist.
        if (whiteList.find(ex.code()) == whiteList.end()) {
            throw;
        }

        // If the document applies to the filter (which means that it should have never been
        // indexed), do not suppress the error.
        const MatchExpression* filter = _indexCatalogEntry->getFilterExpression();
        if (mode == GetKeysMode::kRelaxConstraintsUnfiltered && filter &&
            filter->matchesBSON(obj)) {
            throw;
        }

        onSuppressedError(ex.toStatus(), obj, id);
    }
}

bool AbstractIndexAccessMethod::shouldMarkIndexAsMultikey(
    size_t numberOfKeys,
    const KeyStringSet& multikeyMetadataKeys,
    const MultikeyPaths& multikeyPaths) const {
    return numberOfKeys > 1 || isMultikeyFromPaths(multikeyPaths);
}

SortedDataInterface* AbstractIndexAccessMethod::getSortedDataInterface() const {
    return _newInterface.get();
}

/**
 * Generates a new file name on each call using a static, atomic and monotonically increasing
 * number.
 *
 * Each user of the Sorter must implement this function to ensure that all temporary files that the
 * Sorter instances produce are uniquely identified using a unique file name extension with separate
 * atomic variable. This is necessary because the sorter.cpp code is separately included in multiple
 * places, rather than compiled in one place and linked, and so cannot provide a globally unique ID.
 */
std::string nextFileName() {
    static AtomicWord<unsigned> indexAccessMethodFileCounter;
    return "extsort-index." + std::to_string(indexAccessMethodFileCounter.fetchAndAdd(1));
}

Status AbstractIndexAccessMethod::_handleDuplicateKey(OperationContext* opCtx,
                                                      const KeyString::Value& dataKey,
                                                      const RecordIdHandlerFn& onDuplicateRecord) {
    RecordId recordId = KeyString::decodeRecordIdAtEnd(dataKey.getBuffer(), dataKey.getSize());
    if (onDuplicateRecord) {
        return onDuplicateRecord(recordId);
    }

    BSONObj dupKey = KeyString::toBson(dataKey, getSortedDataInterface()->getOrdering());
    return buildDupKeyErrorStatus(dupKey.getOwned(),
                                  _indexCatalogEntry->ns(),
                                  _descriptor->indexName(),
                                  _descriptor->keyPattern(),
                                  _descriptor->collation());
}
}  // namespace mongo

#include "mongo/db/sorter/sorter.cpp"
MONGO_CREATE_SORTER(mongo::KeyString::Value, mongo::NullValue, mongo::BtreeExternalSortComparison);
