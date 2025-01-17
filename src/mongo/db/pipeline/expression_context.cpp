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

#include "mongo/platform/basic.h"

#include <utility>

#include "mongo/db/pipeline/expression_context.h"
#include "mongo/db/pipeline/process_interface/stub_mongo_process_interface.h"
#include "mongo/db/query/collation/collation_spec.h"
#include "mongo/db/query/collation/collator_factory_interface.h"
#include "mongo/db/stats/counters.h"
#include "mongo/util/intrusive_counter.h"

namespace mongo {

using boost::intrusive_ptr;

ExpressionContext::ResolvedNamespace::ResolvedNamespace(NamespaceString ns,
                                                        std::vector<BSONObj> pipeline)
    : ns(std::move(ns)), pipeline(std::move(pipeline)) {}

ExpressionContext::ExpressionContext(OperationContext* opCtx,
                                     const AggregationRequest& request,
                                     std::unique_ptr<CollatorInterface> collator,
                                     std::shared_ptr<MongoProcessInterface> processInterface,
                                     StringMap<ResolvedNamespace> resolvedNamespaces,
                                     boost::optional<UUID> collUUID)
    : ExpressionContext(opCtx,
                        request.getExplain(),
                        request.isFromMongos(),
                        request.needsMerge(),
                        request.shouldAllowDiskUse(),
                        request.shouldBypassDocumentValidation(),
                        request.getIsMapReduceCommand(),
                        request.getNamespaceString(),
                        request.getRuntimeConstants(),
                        std::move(collator),
                        std::move(processInterface),
                        std::move(resolvedNamespaces),
                        std::move(collUUID)) {
    // Any request which did not originate from a mongoS, or which did originate from a mongoS but
    // has the 'useNewUpsert' flag set, can use the new upsertSupplied mechanism for $merge.
    // TODO SERVER-44884: Remove this flag after we branch for 4.5.
    useNewUpsert = request.getUseNewUpsert() || !request.isFromMongos();

    if (request.getIsMapReduceCommand()) {
        // mapReduce command JavaScript invocation is only subject to the server global
        // 'jsHeapLimitMB' limit.
        jsHeapLimitMB = boost::none;
    }
}

ExpressionContext::ExpressionContext(
    OperationContext* opCtx,
    const boost::optional<ExplainOptions::Verbosity>& explain,
    bool fromMongos,
    bool needsMerge,
    bool allowDiskUse,
    bool bypassDocumentValidation,
    bool isMapReduce,
    const NamespaceString& ns,
    const boost::optional<RuntimeConstants>& runtimeConstants,
    std::unique_ptr<CollatorInterface> collator,
    const std::shared_ptr<MongoProcessInterface>& mongoProcessInterface,
    StringMap<ExpressionContext::ResolvedNamespace> resolvedNamespaces,
    boost::optional<UUID> collUUID)
    : explain(explain),
      fromMongos(fromMongos),
      needsMerge(needsMerge),
      allowDiskUse(allowDiskUse),
      bypassDocumentValidation(bypassDocumentValidation),
      ns(ns),
      uuid(std::move(collUUID)),
      opCtx(opCtx),
      mongoProcessInterface(mongoProcessInterface),
      timeZoneDatabase(opCtx && opCtx->getServiceContext()
                           ? TimeZoneDatabase::get(opCtx->getServiceContext())
                           : nullptr),
      variablesParseState(variables.useIdGenerator()),
      _collator(std::move(collator)),
      _documentComparator(_collator.get()),
      _valueComparator(_collator.get()),
      _resolvedNamespaces(std::move(resolvedNamespaces)) {

    if (runtimeConstants) {
        variables.setRuntimeConstants(*runtimeConstants);
    } else {
        variables.setDefaultRuntimeConstants(opCtx);
    }

    if (!isMapReduce) {
        jsHeapLimitMB = internalQueryJavaScriptHeapSizeLimitMB.load();
    }

    // Any request which did not originate from a mongoS can use the new upsertSupplied mechanism.
    // This is used to set 'useNewUpsert' when constructing a MR context on mongoS or mongoD. The MR
    // on mongoS will be issued as an aggregation to the shards and will use the other constructor.
    // TODO SERVER-44884: Remove this flag after we branch for 4.5.
    useNewUpsert = !fromMongos;
}

ExpressionContext::ExpressionContext(OperationContext* opCtx,
                                     std::unique_ptr<CollatorInterface> collator,
                                     const NamespaceString& nss,
                                     const boost::optional<RuntimeConstants>& runtimeConstants)
    : ns(nss),
      opCtx(opCtx),
      mongoProcessInterface(std::make_shared<StubMongoProcessInterface>()),
      timeZoneDatabase(opCtx && opCtx->getServiceContext()
                           ? TimeZoneDatabase::get(opCtx->getServiceContext())
                           : nullptr),
      variablesParseState(variables.useIdGenerator()),
      _collator(std::move(collator)),
      _documentComparator(_collator.get()),
      _valueComparator(_collator.get()) {
    if (runtimeConstants) {
        variables.setRuntimeConstants(*runtimeConstants);
    } else {
        variables.setDefaultRuntimeConstants(opCtx);
    }

    jsHeapLimitMB = internalQueryJavaScriptHeapSizeLimitMB.load();
}

void ExpressionContext::checkForInterrupt() {
    // This check could be expensive, at least in relative terms, so don't check every time.
    if (--_interruptCounter == 0) {
        invariant(opCtx);
        _interruptCounter = kInterruptCheckPeriod;
        opCtx->checkForInterrupt();
    }
}

ExpressionContext::CollatorStash::CollatorStash(
    const boost::intrusive_ptr<ExpressionContext>& expCtx,
    std::unique_ptr<CollatorInterface> newCollator)
    : _expCtx(expCtx), _originalCollator(std::move(_expCtx->_collator)) {
    _expCtx->setCollator(std::move(newCollator));
}

ExpressionContext::CollatorStash::~CollatorStash() {
    _expCtx->setCollator(std::move(_originalCollator));
}

std::unique_ptr<ExpressionContext::CollatorStash> ExpressionContext::temporarilyChangeCollator(
    std::unique_ptr<CollatorInterface> newCollator) {
    // This constructor of CollatorStash is private, so we can't use make_unique().
    return std::unique_ptr<CollatorStash>(new CollatorStash(this, std::move(newCollator)));
}

intrusive_ptr<ExpressionContext> ExpressionContext::copyWith(
    NamespaceString ns,
    boost::optional<UUID> uuid,
    boost::optional<std::unique_ptr<CollatorInterface>> updatedCollator) const {

    auto collator = updatedCollator
        ? std::move(*updatedCollator)
        : (_collator ? _collator->clone() : std::unique_ptr<CollatorInterface>{});

    auto expCtx = make_intrusive<ExpressionContext>(opCtx,
                                                    explain,
                                                    fromMongos,
                                                    needsMerge,
                                                    allowDiskUse,
                                                    bypassDocumentValidation,
                                                    false,  // isMapReduce
                                                    ns,
                                                    boost::none,  // runtimeConstants
                                                    std::move(collator),
                                                    mongoProcessInterface,
                                                    _resolvedNamespaces,
                                                    uuid);

    expCtx->inMongos = inMongos;
    expCtx->maxFeatureCompatibilityVersion = maxFeatureCompatibilityVersion;
    expCtx->subPipelineDepth = subPipelineDepth;
    expCtx->tempDir = tempDir;
    expCtx->useNewUpsert = useNewUpsert;
    expCtx->jsHeapLimitMB = jsHeapLimitMB;

    expCtx->variables = variables;
    expCtx->variablesParseState = variablesParseState.copyWith(expCtx->variables.useIdGenerator());

    // Note that we intentionally skip copying the value of '_interruptCounter' because 'expCtx' is
    // intended to be used for executing a separate aggregation pipeline.

    return expCtx;
}

void ExpressionContext::startExpressionCounters() {
    if (!_expressionCounters) {
        _expressionCounters = boost::make_optional<ExpressionCounters>({});
    }
}

void ExpressionContext::incrementMatchExprCounter(StringData name) {
    if (_expressionCounters) {
        ++_expressionCounters.get().matchExprCountersMap[name];
    }
}

void ExpressionContext::stopExpressionCounters() {
    if (_expressionCounters) {
        operatorCountersMatchExpressions.mergeCounters(
            _expressionCounters.get().matchExprCountersMap);
    }
    _expressionCounters = boost::none;
}

}  // namespace mongo
