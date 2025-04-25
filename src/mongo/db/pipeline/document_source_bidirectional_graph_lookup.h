/**
 *    Copyright (C) 2025-present MongoDB, Inc.
 */

 #pragma once

 #include "mongo/db/pipeline/document_source.h"
 #include "mongo/db/pipeline/document_source_graph_lookup.h"
 #include "mongo/db/exec/document_value/value_comparator.h"
 #include <queue>
 #include <unordered_map>
 #include <set>
 
 namespace mongo {
 
 class DocumentSourceBidirectionalGraphLookup final : public DocumentSource {
 public:
     static constexpr StringData kStageName = "$bidirectionalGraphLookup"_sd;
     static const Id& id;
 
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
 
     static boost::intrusive_ptr<DocumentSource> createFromBson(
         BSONElement elem, const boost::intrusive_ptr<ExpressionContext>& pExpCtx);
 
     DocumentSourceBidirectionalGraphLookup(const boost::intrusive_ptr<ExpressionContext>& pExpCtx,
                                            NamespaceString fromNs,
                                            std::string asField,
                                            std::string connectFromField,
                                            std::string connectToField,
                                            boost::intrusive_ptr<Expression> startWith,
                                            boost::intrusive_ptr<Expression> target,
                                            boost::optional<BSONObj> restrictSearchWithMatch,
                                            boost::optional<std::string> depthField,
                                            boost::optional<long long> maxDepth);
 
     ~DocumentSourceBidirectionalGraphLookup() override = default;
 
     // DocumentSource interface
     const char* getSourceName() const final;
     Id getId() const final;
     void addVariableRefs(std::set<Variables::Id>* refs) const final;
     
     boost::intrusive_ptr<DocumentSource> clone(
         const boost::intrusive_ptr<ExpressionContext>& pExpCtx) const final;
         
     Value serialize(const SerializationOptions& opts = SerializationOptions{}) const final;
     void serializeToArray(std::vector<Value>& array,
                           const SerializationOptions& opts = SerializationOptions{}) const final;
     
     GetModPathsReturn getModifiedPaths() const final;
     StageConstraints constraints(Pipeline::SplitState pipeState) const final;
     boost::optional<DistributedPlanLogic> distributedPlanLogic() final;
     
     DepsTracker::State getDependencies(DepsTracker* deps) const final;
     
     void addInvolvedCollections(stdx::unordered_set<NamespaceString>* collectionNames) const final;
     void detachFromOperationContext() final;
     void reattachToOperationContext(OperationContext* opCtx) final;
     bool validateOperationContext(const OperationContext* opCtx) const final;
 
 protected:
     GetNextResult doGetNext() final;
     void doDispose() final;
     boost::optional<ShardId> computeMergeShardId() const final;
 
 private:
     struct SearchNode {
         Value id;
         size_t depth;
         std::vector<Value> path;
         bool isForwardDirection;
         Value parentId;
     };
 
     struct BidirectionalPath {
         std::vector<Document> forwardPath;
         std::vector<Document> backwardPath;
         Document meetingNode;
     };
 
     // Core algorithm methods
     void performBidirectionalSearch();
     bool expandFrontier(bool isForward);
     boost::optional<Value> checkMeetingPoint();
     std::vector<Document> reconstructPath(Value meetingId);
     BSONObj makeMatchStageFromFrontier(const ValueFlatUnorderedSet& frontier);
     
     std::unique_ptr<Pipeline, PipelineDeleter> buildPipeline(const BSONObj& match);
     void checkMemoryUsage();
     void resetSearchState();
 
     // Configuration
     NamespaceString _fromNs;
     std::string _as;
     std::string _connectFromField;
     std::string _connectToField;
     boost::intrusive_ptr<Expression> _startWith;
     boost::intrusive_ptr<Expression> _target;
     boost::optional<BSONObj> _additionalFilter;
     boost::optional<std::string> _depthField;
     boost::optional<long long> _maxDepth;
     
     // ExpressionContext for the from collection
     boost::intrusive_ptr<ExpressionContext> _fromExpCtx;
     
     // Search state
     bool _executed = false;
     Document _inputDoc;
     std::vector<Document> _results;
     
     // Bidirectional search data structures
     ValueUnorderedMap<SearchNode> _forwardVisited;
     ValueUnorderedMap<SearchNode> _backwardVisited;
     ValueFlatUnorderedSet _forwardFrontier;
     ValueFlatUnorderedSet _backwardFrontier;
     
     // Memory tracking
     size_t _maxMemoryUsageBytes;
     size_t _frontierUsageBytes = 0;
     size_t _visitedUsageBytes = 0;
 };
 
 }  // namespace mongo