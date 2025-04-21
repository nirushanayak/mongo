// document_source_bidirectional_graph_lookup.cpp

#include "mongo/db/pipeline/document_source_bidirectional_graph_lookup.h"
#include "mongo/db/pipeline/expression_context.h"
#include "mongo/db/pipeline/lite_parsed_pipeline.h"
#include "mongo/db/pipeline/document_source_match.h"
#include "mongo/util/namespace_string_util.h"
#include "mongo/db/exec/document_value/document.h"
#include "mongo/db/exec/document_value/value_comparator.h"
#include "mongo/db/pipeline/document_path_support.h"
#include "mongo/db/pipeline/pipeline.h"
#include "mongo/db/views/resolved_view.h"
// #include "mongo/logv2/log.h"

namespace mongo {

REGISTER_DOCUMENT_SOURCE(bidirectionalGraphLookup,
                         DocumentSourceBidirectionalGraphLookup::LiteParsed::parse,
                         DocumentSourceBidirectionalGraphLookup::createFromBson,
                         AllowedWithApiStrict::kAlways);

ALLOCATE_DOCUMENT_SOURCE_ID(bidirectionalGraphLookup, DocumentSourceBidirectionalGraphLookup::id)

// LOGV2(9990001, "$bidirectionalGraphLookup registered successfully");

namespace {
NamespaceString parseGraphLookupFromAndResolveNamespace(const BSONElement& elem,
                                                        const DatabaseName& defaultDb) {
    uassert(ErrorCodes::FailedToParse,
            str::stream() << "$graphLookup 'from' field must be a string, but found "
                          << typeName(elem.type()),
            elem.type() == String);

    NamespaceString fromNss = NamespaceStringUtil::deserialize(defaultDb, elem.valueStringData());
    uassert(ErrorCodes::InvalidNamespace,
            str::stream() << "invalid $graphLookup namespace: " << fromNss.toStringForErrorMsg(),
            fromNss.isValid());
    return fromNss;
}
}  // namespace

std::unique_ptr<DocumentSourceBidirectionalGraphLookup::LiteParsed>
DocumentSourceBidirectionalGraphLookup::LiteParsed::parse(
    const NamespaceString& nss, const BSONElement& spec, const LiteParserOptions& options) {
    uassert(ErrorCodes::FailedToParse,
            "$bidirectionalGraphLookup must be an object",
            spec.type() == Object);

    auto specObj = spec.Obj();
    auto fromElement = specObj["from"];
    uassert(ErrorCodes::FailedToParse,
            "missing 'from' option to $bidirectionalGraphLookup stage",
            fromElement);

    NamespaceString fromNs = parseGraphLookupFromAndResolveNamespace(fromElement, nss.dbName());

    return std::make_unique<LiteParsed>(spec.fieldName(), std::move(fromNs));
}

PrivilegeVector DocumentSourceBidirectionalGraphLookup::LiteParsed::requiredPrivileges(
    bool isMongos, bool bypassDocumentValidation) const {
    return {Privilege(ResourcePattern::forExactNamespace(_foreignNss), ActionType::find)};
}

boost::intrusive_ptr<DocumentSource> DocumentSourceBidirectionalGraphLookup::createFromBson(
    BSONElement elem, const boost::intrusive_ptr<ExpressionContext>& pExpCtx) {
    auto spec = elem.Obj();
    auto stage = make_intrusive<DocumentSourceBidirectionalGraphLookup>(pExpCtx);
    VariablesParseState vps = pExpCtx->variablesParseState;

    stage->_fromNs = parseGraphLookupFromAndResolveNamespace(spec["from"], pExpCtx->getNamespaceString().dbName());
    stage->_connectFromField = spec["connectFromField"].str();
    stage->_connectToField = spec["connectToField"].str();
    stage->_as = spec["as"].str();
    stage->_startWith = Expression::parseOperand(pExpCtx.get(), spec["startWith"], vps);
    stage->_target = Expression::parseOperand(pExpCtx.get(), spec["target"], vps);

    return stage;
}

const char* DocumentSourceBidirectionalGraphLookup::getSourceName() const {
    return kStageName.rawData();
}

DocumentSourceBidirectionalGraphLookup::Id DocumentSourceBidirectionalGraphLookup::getId() const {
    return id;
}

DocumentSource::GetNextResult DocumentSourceBidirectionalGraphLookup::doGetNext() {
    if (_executed)
        return GetNextResult::makeEOF();

    auto input = pSource->getNext();
    if (!input.isAdvanced())
        return input;

    _inputDoc = input.releaseDocument();
    _executed = true;

    auto startVal = _startWith->evaluate(_inputDoc, &pExpCtx->variables);
    auto targetVal = _target->evaluate(_inputDoc, &pExpCtx->variables);

    _results = performBidirectionalSearch(startVal, targetVal);

    MutableDocument out(_inputDoc);
    out.setField(_as, Value(_results));
    return out.freeze();
}

std::vector<Document> DocumentSourceBidirectionalGraphLookup::performBidirectionalSearch(
    const Value& start, const Value& target) {

    ValueComparator valueCmp;
    std::set<Value, ValueComparator::LessThan> visitedF(valueCmp.getLessThan());
    std::set<Value, ValueComparator::LessThan> visitedB(valueCmp.getLessThan());

    std::queue<Value> frontierF, frontierB;

    frontierF.push(start);
    frontierB.push(target);

    visitedF.insert(start);
    visitedB.insert(target);

    while (!frontierF.empty() && !frontierB.empty()) {
        auto docF = lookupAndExpand(frontierF.front(), visitedF, visitedB, true);
        if (docF)
            return {*docF};
        frontierF.pop();

        auto docB = lookupAndExpand(frontierB.front(), visitedB, visitedF, false);
        if (docB)
            return {*docB};
        frontierB.pop();
    }

    return {};
}

boost::optional<Document> DocumentSourceBidirectionalGraphLookup::lookupAndExpand(
    const Value& current,
    std::set<Value, ValueComparator::LessThan>& ownVisited,
    std::set<Value, ValueComparator::LessThan>& otherVisited,
    bool isForward) {

    BSONObj match = BSON((isForward ? _connectFromField : _connectToField) << current);

std::vector<BSONObj> pipelineBson = {BSON("$match" << match)};
auto fromExpCtx = pExpCtx->copyWith(_fromNs);
fromExpCtx->setInLookup(true);

MakePipelineOptions pipelineOpts;
pipelineOpts.optimize = true;
pipelineOpts.attachCursorSource = true;

auto execPipeline = Pipeline::makePipeline(pipelineBson, fromExpCtx, pipelineOpts);



    std::vector<Document> results;
    for (auto doc = execPipeline->getNext(); doc; doc = execPipeline->getNext()) {
        results.push_back(*doc);
    }

    for (const auto& doc : results) {
        auto id = doc["_id"];
        if (otherVisited.count(id)) {
            return doc;
        }

        if (!ownVisited.count(id)) {
            ownVisited.insert(id);

            document_path_support::visitAllValuesAtPath(
                doc, isForward ? FieldPath(_connectFromField) : FieldPath(_connectToField),
                [&](const Value& val) {
                    if (!ownVisited.count(val)) {
                        ownVisited.insert(val);
                    }
                });
        }
    }

    return boost::none;
}

void DocumentSourceBidirectionalGraphLookup::doDispose() {}

boost::intrusive_ptr<DocumentSource> DocumentSourceBidirectionalGraphLookup::clone(
    const boost::intrusive_ptr<ExpressionContext>& pExpCtx) const {
    return make_intrusive<DocumentSourceBidirectionalGraphLookup>(pExpCtx);
}

Value DocumentSourceBidirectionalGraphLookup::serialize(const SerializationOptions& opts) const {
    MutableDocument container;
    MutableDocument spec;

    spec["from"] = Value(NamespaceStringUtil::serialize(_fromNs, SerializationContext::stateCommandRequest()));
    spec["connectFromField"] = Value(_connectFromField);
    spec["connectToField"] = Value(_connectToField);
    spec["as"] = Value(_as);
    spec["startWith"] = _startWith->serialize(opts);
    spec["target"] = _target->serialize(opts);

    container[getSourceName()] = Value(spec.freeze());
    return container.freezeToValue();
}

void DocumentSourceBidirectionalGraphLookup::serializeToArray(
    std::vector<Value>& array, const SerializationOptions& opts) const {
    array.push_back(serialize(opts));
}

DocumentSource::GetModPathsReturn DocumentSourceBidirectionalGraphLookup::getModifiedPaths() const {
    return {GetModPathsReturn::Type::kFiniteSet, {_as}, {}};
}

StageConstraints DocumentSourceBidirectionalGraphLookup::constraints(
    Pipeline::SplitState pipeState) const {
    return StageConstraints(StreamType::kStreaming,
                            PositionRequirement::kNone,
                            HostTypeRequirement::kNone,
                            DiskUseRequirement::kNoDiskUse,
                            FacetRequirement::kAllowed,
                            TransactionRequirement::kAllowed,
                            LookupRequirement::kAllowed,
                            UnionRequirement::kAllowed);
}

boost::optional<DocumentSource::DistributedPlanLogic> DocumentSourceBidirectionalGraphLookup::distributedPlanLogic() {
    return boost::none;
}

boost::optional<ShardId> DocumentSourceBidirectionalGraphLookup::computeMergeShardId() const {
    return boost::none;
}

void DocumentSourceBidirectionalGraphLookup::detachFromOperationContext() {}

void DocumentSourceBidirectionalGraphLookup::reattachToOperationContext(OperationContext* opCtx) {}

}  // namespace mongo
