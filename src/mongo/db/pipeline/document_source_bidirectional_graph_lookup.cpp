#include "mongo/db/pipeline/document_source_bidirectional_graph_lookup.h"

#include <algorithm>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "mongo/db/exec/document_value/document.h"
#include "mongo/db/exec/document_value/value.h"
#include "mongo/db/pipeline/expression.h"
#include "mongo/db/pipeline/expression_context.h"

namespace mongo {

// We need to manually handle document registration in a different way since
// the modern MongoDB API is different from what we expected
namespace {
// Document source registration - using a "self-registration" approach
struct RegisterBidirectionalGraphLookup {
    RegisterBidirectionalGraphLookup() {
        // Register our document source
        static const auto kRegistry = Registry::get(getGlobalServiceContext());
        DocumentSource::registerParser(
            DocumentSourceBidirectionalGraphLookup::kStageName.toString(),
            [](BSONElement elem, const boost::intrusive_ptr<ExpressionContext>& expCtx) {
                return DocumentSourceBidirectionalGraphLookup::createFromBson(elem, expCtx);
            });
    }
} registerBidirectionalGraphLookup;
}

std::unique_ptr<DocumentSourceBidirectionalGraphLookup::LiteParsed>
DocumentSourceBidirectionalGraphLookup::LiteParsed::parse(const BSONElement& spec) {
    uassert(ErrorCodes::FailedToParse,
            str::stream() << "the " << DocumentSourceBidirectionalGraphLookup::kStageName
                       << " stage specification must be an object",
            spec.type() == BSONType::Object);

    auto specObj = spec.Obj();
    auto fromElement = specObj["from"];
    uassert(ErrorCodes::FailedToParse,
            str::stream() << "missing 'from' option to "
                       << DocumentSourceBidirectionalGraphLookup::kStageName,
            fromElement.type() == BSONType::String);

    // Create a correct NamespaceString - the API seems to have changed
    // Let's try using the constructor in different ways
    NamespaceString fromNss;
    try {
        // Try with database + collection
        fromNss = NamespaceString("admin." + fromElement.valueStringData());
    } catch (const DBException& ex) {
        uasserted(ErrorCodes::FailedToParse,
                 str::stream() << "invalid foreign collection: " << fromElement.str());
    }

    return std::make_unique<DocumentSourceBidirectionalGraphLookup::LiteParsed>(
        DocumentSourceBidirectionalGraphLookup::kStageName.toString(), std::move(fromNss));
}

PrivilegeVector DocumentSourceBidirectionalGraphLookup::LiteParsed::requiredPrivileges(
    bool isMongos, bool bypassDocumentValidation) const {
    PrivilegeVector privileges;
    privileges.push_back({ResourcePattern::forExactNamespace(_foreignNss), ActionType::find});
    return privileges;
}

stdx::unordered_set<NamespaceString>
DocumentSourceBidirectionalGraphLookup::LiteParsed::getInvolvedNamespaces() const {
    return {_foreignNss};
}

boost::intrusive_ptr<DocumentSource> DocumentSourceBidirectionalGraphLookup::createFromBson(
    BSONElement elem, const boost::intrusive_ptr<ExpressionContext>& pExpCtx) {
    
    uassert(ErrorCodes::FailedToParse,
            str::stream() << "the " << kStageName << " stage specification must be an object",
            elem.type() == BSONType::Object);

    auto specObj = elem.Obj();

    // Parse required fields
    std::string fromFieldName = parseRequiredStringField(specObj, "from");
    std::string asFieldName = parseRequiredStringField(specObj, "as");
    std::string connectToFieldName = parseRequiredStringField(specObj, "connectToField");
    std::string connectFromFieldName = parseRequiredStringField(specObj, "connectFromField");

    // Parse optional fields
    boost::optional<BSONObj> startWith;
    if (specObj.hasField("startWith")) {
        startWith = specObj["startWith"].Obj().getOwned();
    }

    boost::optional<BSONObj> endWith;
    if (specObj.hasField("endWith")) {
        endWith = specObj["endWith"].Obj().getOwned();
    }

    boost::optional<std::string> depthField;
    if (specObj.hasField("depthField")) {
        depthField = parseRequiredStringField(specObj, "depthField");
    }

    boost::optional<long long> maxDepth;
    if (specObj.hasField("maxDepth")) {
        uassert(ErrorCodes::FailedToParse,
                str::stream() << "maxDepth must be a number, but got " 
                           << typeName(specObj["maxDepth"].type()),
                specObj["maxDepth"].isNumber());
        maxDepth = specObj["maxDepth"].safeNumberLong();
    }

    // Create namespace string - using a simpler approach since pExpCtx->ns is not available
    NamespaceString fromNs("admin." + fromFieldName);

    // Create and return the pipeline stage
    return boost::intrusive_ptr<DocumentSource>(new DocumentSourceBidirectionalGraphLookup(
        pExpCtx,
        std::move(fromNs),
        asFieldName,
        connectToFieldName,
        connectFromFieldName,
        startWith,
        endWith,
        depthField,
        maxDepth));
}

DocumentSourceBidirectionalGraphLookup::DocumentSourceBidirectionalGraphLookup(
    const boost::intrusive_ptr<ExpressionContext>& pExpCtx,
    NamespaceString fromNs,
    std::string asField,
    std::string connectToField,
    std::string connectFromField,
    boost::optional<BSONObj> startWith,
    boost::optional<BSONObj> endWith,
    boost::optional<std::string> depthField,
    boost::optional<long long> maxDepth)
    : DocumentSource(kStageName, pExpCtx),
      _fromNs(std::move(fromNs)),
      _asField(std::move(asField)),
      _connectToField(std::move(connectToField)),
      _connectFromField(std::move(connectFromField)),
      _startWith(std::move(startWith)),
      _endWith(std::move(endWith)),
      _depthField(std::move(depthField)),
      _maxDepth(std::move(maxDepth)) {}

StageConstraints DocumentSourceBidirectionalGraphLookup::constraints(Pipeline::SplitState pipeState) const {
    return StageConstraints(
        StreamType::kStreaming,
        PositionRequirement::kNone,
        HostTypeRequirement::kNone,
        DiskUseRequirement::kNoDiskUse,
        FacetRequirement::kAllowed,
        TransactionRequirement::kAllowed,
        LookupRequirement::kAllowed,
        UnionRequirement::kAllowed);
}

DocumentSource::GetNextResult DocumentSourceBidirectionalGraphLookup::doGetNext() {
    if (_sourceExhausted) {
        return GetNextResult::makeEOF();
    }

    if (_results.empty()) {
        // Get the next input document
        auto nextInput = pSource->getNext();
        if (nextInput.isEOF()) {
            _sourceExhausted = true;
            return GetNextResult::makeEOF();
        }

        // Process the input document through bidirectional algorithm
        return performBidirectionalSearch(nextInput.releaseDocument());
    }

    // Return the next queued result
    auto nextResult = std::move(_results.front());
    _results.pop();
    return DocumentSource::GetNextResult(std::move(nextResult));
}

DocumentSource::GetNextResult DocumentSourceBidirectionalGraphLookup::performBidirectionalSearch(
    const Document& inputDoc) {
    
    // This is a placeholder implementation
    // In a real implementation, you would implement the bidirectional search algorithm
    
    // For now, just return the input document with an empty array field
    MutableDocument result(inputDoc);
    result.setField(_asField, Value(std::vector<Value>{}));
    return DocumentSource::GetNextResult(result.freeze());
}

std::vector<Value> DocumentSourceBidirectionalGraphLookup::getStartIds(const Document& inputDoc) {
    // Placeholder implementation
    return std::vector<Value>{};
}

std::vector<Value> DocumentSourceBidirectionalGraphLookup::getTargetIds(const Document& inputDoc) {
    // Placeholder implementation
    return std::vector<Value>{};
}

std::vector<Value> DocumentSourceBidirectionalGraphLookup::expandForward(const Value& nodeId) {
    // Placeholder implementation
    return std::vector<Value>{};
}

std::vector<Value> DocumentSourceBidirectionalGraphLookup::expandBackward(const Value& nodeId) {
    // Placeholder implementation
    return std::vector<Value>{};
}

boost::optional<Document> DocumentSourceBidirectionalGraphLookup::reconstructPath(Value meetingId) {
    // Placeholder implementation
    return boost::none;
}

std::vector<Document> DocumentSourceBidirectionalGraphLookup::lookupDocumentsByIds(
    const std::vector<Value>& ids) {
    // Placeholder implementation
    return std::vector<Document>{};
}

std::vector<Document> DocumentSourceBidirectionalGraphLookup::findDocumentsThatReferenceId(
    const Value& id) {
    // Placeholder implementation
    return std::vector<Document>{};
}

Value DocumentSourceBidirectionalGraphLookup::serialize(const SerializationOptions& opts) const {
    MutableDocument result;
    
    Document config(
        {{"from", Value(_fromNs.coll())},
         {"as", Value(_asField)},
         {"connectToField", Value(_connectToField)},
         {"connectFromField", Value(_connectFromField)}});
    
    MutableDocument configDoc(config);
    
    if (_startWith) {
        configDoc.setField("startWith", Value(_startWith.get()));
    }
    
    if (_endWith) {
        configDoc.setField("endWith", Value(_endWith.get()));
    }
    
    if (_depthField) {
        configDoc.setField("depthField", Value(*_depthField));
    }
    
    if (_maxDepth) {
        configDoc.setField("maxDepth", Value(*_maxDepth));
    }
    
    result.setField(kStageName.toString(), Value(configDoc.freeze()));
    return Value(result.freeze());
}

// Helper function to parse required string fields from BSON
std::string parseRequiredStringField(const BSONObj& obj, StringData fieldName) {
    auto elem = obj[fieldName];
    uassert(ErrorCodes::FailedToParse,
            str::stream() << "missing required field '" << fieldName << "'",
            !elem.eoo());
    
    uassert(ErrorCodes::FailedToParse,
            str::stream() << "'" << fieldName << "' must be a string, not " 
                       << typeName(elem.type()),
            elem.type() == BSONType::String);
            
    return elem.String();
}

} // namespace mongo