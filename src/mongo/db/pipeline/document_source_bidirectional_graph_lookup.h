#pragma once

#include <cstddef>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/optional/optional.hpp>
#include <boost/smart_ptr/intrusive_ptr.hpp>

#include "mongo/base/error_codes.h"
#include "mongo/base/string_data.h"
#include "mongo/bson/bsonelement.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/db/auth/action_type.h"
#include "mongo/db/auth/privilege.h"
#include "mongo/db/auth/resource_pattern.h"
#include "mongo/db/exec/document_value/document.h"
#include "mongo/db/exec/document_value/value.h"
#include "mongo/db/exec/document_value/value_comparator.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/pipeline/document_source.h"
#include "mongo/db/pipeline/expression_context.h"
#include "mongo/db/pipeline/lite_parsed_document_source.h"
#include "mongo/db/pipeline/stage_constraints.h"
#include "mongo/stdx/unordered_set.h"

namespace mongo {

/**
 * This stage implements bidirectional graph lookup - an optimized path finding algorithm
 * that searches from both start and end nodes simultaneously to find paths more efficiently
 * than standard BFS used in $graphLookup.
 */
class DocumentSourceBidirectionalGraphLookup : public DocumentSource {
public:
    static constexpr StringData kStageName = "$bidirectionalGraphLookup"_sd;

    class LiteParsed final : public LiteParsedDocumentSource {
    public:
        static std::unique_ptr<LiteParsed> parse(const BSONElement& spec);

        LiteParsed(std::string parseTimeName, NamespaceString foreignNss)
            : LiteParsedDocumentSource(std::move(parseTimeName)), _foreignNss(std::move(foreignNss)) {}

        PrivilegeVector requiredPrivileges(bool isMongos, 
                                          bool bypassDocumentValidation) const override;

        stdx::unordered_set<NamespaceString> getInvolvedNamespaces() const override;

    private:
        const NamespaceString _foreignNss;
    };

    static boost::intrusive_ptr<DocumentSource> createFromBson(
        BSONElement elem, 
        const boost::intrusive_ptr<ExpressionContext>& pExpCtx);

    // Implement required DocumentSource pure virtual methods
    const char* getSourceName() const override { return kStageName.rawData(); }
    
    // Implement the pipeline stage logic pure virtual methods
    StageConstraints constraints(Pipeline::SplitState pipeState) const override;
    Value serialize(const SerializationOptions& opts = SerializationOptions{}) const override;
    
    // Variable reference tracking
    void addVariableRefs(std::set<Variables::Id>* refs) const override {
        // No variables used in this stage
    }
    
    // Distributed plan logic
    boost::optional<DistributedPlanLogic> distributedPlanLogic() override {
        return boost::none;  // Not distributable
    }

protected:
    // This is the core processing method
    GetNextResult doGetNext() override;

private:
    // Custom hasher and equality for Value
    struct ValueHasher {
        size_t operator()(const Value& v) const {
            return std::hash<std::string>{}(v.toString());
        }
    };

    struct ValueEqualTo {
        bool operator()(const Value& lhs, const Value& rhs) const {
            return ValueComparator().compare(lhs, rhs) == 0;
        }
    };

    // Internal data structures for the bidirectional search
    struct SearchNode {
        Value id;
        size_t depth;
        boost::optional<Value> parentId;
    };

    // Constructor - private, use createFromBson instead
    DocumentSourceBidirectionalGraphLookup(
        const boost::intrusive_ptr<ExpressionContext>& pExpCtx,
        NamespaceString fromNs,
        std::string asField,
        std::string connectToField,
        std::string connectFromField,
        boost::optional<BSONObj> startWith,
        boost::optional<BSONObj> endWith,
        boost::optional<std::string> depthField,
        boost::optional<long long> maxDepth);

    // Core algorithm implementation
    GetNextResult performBidirectionalSearch(const Document& inputDoc);
    
    // Helper methods
    std::vector<Value> getStartIds(const Document& inputDoc);
    std::vector<Value> getTargetIds(const Document& inputDoc);
    std::vector<Value> expandForward(const Value& nodeId);
    std::vector<Value> expandBackward(const Value& nodeId);
    boost::optional<Document> reconstructPath(Value meetingId);

    // Database access methods
    std::vector<Document> lookupDocumentsByIds(const std::vector<Value>& ids);
    std::vector<Document> findDocumentsThatReferenceId(const Value& id);
    
    // Configuration
    NamespaceString _fromNs;
    std::string _asField;
    std::string _connectToField;
    std::string _connectFromField;
    boost::optional<BSONObj> _startWith;
    boost::optional<BSONObj> _endWith;
    boost::optional<std::string> _depthField;
    boost::optional<long long> _maxDepth;
    
    // Algorithm state
    bool _sourceExhausted = false;
    std::queue<Document> _results;
    
    // Bidirectional search state with custom hashers
    std::unordered_map<Value, SearchNode, ValueHasher, ValueEqualTo> _forwardVisited;
    std::unordered_map<Value, SearchNode, ValueHasher, ValueEqualTo> _backwardVisited;
    std::queue<Value> _forwardQueue;
    std::queue<Value> _backwardQueue;
    std::vector<Document> _allNodes;
    
    // Cache for lookups
    std::unordered_map<Value, Document, ValueHasher, ValueEqualTo> _documentCache;
};

// Helper function declaration
std::string parseRequiredStringField(const BSONObj& obj, StringData fieldName);

} // namespace mongo