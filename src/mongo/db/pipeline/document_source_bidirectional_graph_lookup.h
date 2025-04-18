/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 */

 #pragma once

 #include "mongo/db/pipeline/document_source.h"
 #include "mongo/db/pipeline/document_source_graph_lookup.h"
 
 namespace mongo {
 
 /**
  * $bidirectionalGraphLookup is a stage that performs a graph lookup using a bidirectional search.
  */
 class DocumentSourceBidirectionalGraphLookup final : public DocumentSource {
 public:
     static constexpr StringData kStageName = "$bidirectionalGraphLookup"_sd;
     static const Id& id;
     
     // Required implementations for DocumentSource
     const char* getSourceName() const final;
     Id getId() const final;
     
     // Create a simple implementation for addVariableRefs (required to avoid 'abstract class' error)
     void addVariableRefs(std::set<Variables::Id>* refs) const final {}
     
     boost::intrusive_ptr<DocumentSource> clone(
         const boost::intrusive_ptr<ExpressionContext>& pExpCtx) const final;
     Value serialize(const SerializationOptions& opts = SerializationOptions{}) const final;
     void serializeToArray(std::vector<Value>& array, 
                          const SerializationOptions& opts = SerializationOptions{}) const final;
     GetModPathsReturn getModifiedPaths() const final;
     StageConstraints constraints(Pipeline::SplitState pipeState) const final;
     boost::optional<DistributedPlanLogic> distributedPlanLogic() final;
     
     // Static factory method for creating from BSON
     static boost::intrusive_ptr<DocumentSource> createFromBson(
         BSONElement elem, const boost::intrusive_ptr<ExpressionContext>& pExpCtx);
         
     // Constructors
     explicit DocumentSourceBidirectionalGraphLookup(const boost::intrusive_ptr<ExpressionContext>& pExpCtx)
         : DocumentSource(kStageName, pExpCtx) {}
         
     // LiteParsed class for early parsing
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
 };
 
 }  // namespace mongo