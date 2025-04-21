// document_source_bidirectional_graph_lookup.h

#pragma once

#include "mongo/db/pipeline/document_source.h"
#include "mongo/db/pipeline/document_source_graph_lookup.h"
#include "mongo/db/exec/document_value/value_comparator.h"

namespace mongo {

class DocumentSourceBidirectionalGraphLookup final : public DocumentSource {
public:
    static constexpr StringData kStageName = "$bidirectionalGraphLookup"_sd;
    static const Id& id;

    const char* getSourceName() const final;
    Id getId() const final;

    void addVariableRefs(std::set<Variables::Id>* refs) const final {}

    boost::intrusive_ptr<DocumentSource> clone(
        const boost::intrusive_ptr<ExpressionContext>& pExpCtx) const final;
    Value serialize(const SerializationOptions& opts = SerializationOptions{}) const final;
    void serializeToArray(std::vector<Value>& array,
                          const SerializationOptions& opts = SerializationOptions{}) const final;
    GetModPathsReturn getModifiedPaths() const final;
    StageConstraints constraints(Pipeline::SplitState pipeState) const final;
    boost::optional<DistributedPlanLogic> distributedPlanLogic() final;

    static boost::intrusive_ptr<DocumentSource> createFromBson(
        BSONElement elem, const boost::intrusive_ptr<ExpressionContext>& pExpCtx);

    explicit DocumentSourceBidirectionalGraphLookup(const boost::intrusive_ptr<ExpressionContext>& pExpCtx)
        : DocumentSource(kStageName, pExpCtx) {}

    class LiteParsed final : public LiteParsedDocumentSourceForeignCollection {
    public:
        static std::unique_ptr<LiteParsed> parse(const NamespaceString& nss,
                                                 const BSONElement& spec,
                                                 const LiteParserOptions& options);

        LiteParsed(std::string parseTimeName, NamespaceString foreignNss)
            : LiteParsedDocumentSourceForeignCollection(std::move(parseTimeName),
                                                        std::move(foreignNss)) {}

        PrivilegeVector requiredPrivileges(bool isMongos, bool bypassDocumentValidation) const override;
    };

protected:
    GetNextResult doGetNext() final;
    void doDispose() final;
    boost::optional<ShardId> computeMergeShardId() const final;

private:
    void detachFromOperationContext() final;
    void reattachToOperationContext(OperationContext* opCtx) final;

    // Specified in BSON input
    NamespaceString _fromNs;
    std::string _connectFromField;
    std::string _connectToField;
    std::string _as;
    boost::intrusive_ptr<Expression> _startWith;
    boost::intrusive_ptr<Expression> _target;

    // Execution state
    bool _executed = false;
    Document _inputDoc;
    std::vector<Document> _results;

    std::vector<Document> performBidirectionalSearch(const Value& start, const Value& target);

    boost::optional<Document> lookupAndExpand(
        const Value& current,
        std::set<Value, ValueComparator::LessThan>& ownVisited,
        std::set<Value, ValueComparator::LessThan>& otherVisited,
        bool isForward);
};

}  // namespace mongo