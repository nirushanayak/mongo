/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 */

 #include "mongo/db/pipeline/document_source_bidirectional_graph_lookup.h"
 #include "mongo/db/pipeline/document_source_graph_lookup.h"
 #include "mongo/db/pipeline/expression_context.h"
 #include "mongo/db/pipeline/lite_parsed_pipeline.h"
 
 namespace mongo {
 
 // Register the stage
 REGISTER_DOCUMENT_SOURCE(bidirectionalGraphLookup,
                          DocumentSourceBidirectionalGraphLookup::LiteParsed::parse,
                          DocumentSourceBidirectionalGraphLookup::createFromBson,
                          AllowedWithApiStrict::kAlways);
 
 // Allocate ID for this stage
 ALLOCATE_DOCUMENT_SOURCE_ID(bidirectionalGraphLookup, DocumentSourceBidirectionalGraphLookup::id)

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

    uassert(ErrorCodes::FailedToParse,
            "$bidirectionalGraphLookup 'from' field must be a string",
            fromElement.type() == String);
    
    // Direct namespace construction - try a simpler approach
    NamespaceString fromNs;
    
    // Parse the from namespace directly
    if (fromElement.valueStringData().find('.') == std::string::npos) {
        // Collection in the current database
        // fromNs = NamespaceString(nss.db(), fromElement.valueStringData());
        fromNs = NamespaceStringUtil::deserialize(nss.dbName(), fromElement.valueStringData());

    } else {
        // Fully qualified namespace
        // fromNs = NamespaceString(fromElement.valueStringData());
        fromNs = NamespaceStringUtil::deserialize(nss.dbName(), fromElement.valueStringData());

    }
    
    // Validate the namespace
    uassert(ErrorCodes::InvalidNamespace,
            str::stream() << "invalid $bidirectionalGraphLookup namespace: "
                        << fromNs.toStringForErrorMsg(),
            fromNs.isValid());
    
    return std::make_unique<LiteParsed>(spec.fieldName(), std::move(fromNs));
}
 
 PrivilegeVector DocumentSourceBidirectionalGraphLookup::LiteParsed::requiredPrivileges(
     bool isMongos, bool bypassDocumentValidation) const {
     return {Privilege(ResourcePattern::forExactNamespace(_foreignNss), ActionType::find)};
 }
 
 boost::intrusive_ptr<DocumentSource>
 DocumentSourceBidirectionalGraphLookup::createFromBson(
     BSONElement elem, const boost::intrusive_ptr<ExpressionContext>& pExpCtx) {
     
     // For now, just create the basic stage
     return make_intrusive<DocumentSourceBidirectionalGraphLookup>(pExpCtx);
 }
 
 const char* DocumentSourceBidirectionalGraphLookup::getSourceName() const {
     return kStageName.rawData();
 }
 
 DocumentSourceBidirectionalGraphLookup::Id DocumentSourceBidirectionalGraphLookup::getId() const {
     return id;
 }
 
 DocumentSource::GetNextResult DocumentSourceBidirectionalGraphLookup::doGetNext() {
     // For now, just pass through documents
     return pSource->getNext();
 }
 
 void DocumentSourceBidirectionalGraphLookup::doDispose() {
     // Nothing to dispose yet
 }
 
 boost::intrusive_ptr<DocumentSource>
 DocumentSourceBidirectionalGraphLookup::clone(
     const boost::intrusive_ptr<ExpressionContext>& pExpCtx) const {
     
     return make_intrusive<DocumentSourceBidirectionalGraphLookup>(pExpCtx);
 }
 
 Value DocumentSourceBidirectionalGraphLookup::serialize(
     const SerializationOptions& opts) const {
     
     MutableDocument container;
     container[getSourceName()] = Value(Document{});
     return container.freezeToValue();
 }
 
 void DocumentSourceBidirectionalGraphLookup::serializeToArray(
     std::vector<Value>& array, const SerializationOptions& opts) const {
     
     array.push_back(serialize(opts));
 }
 
 DocumentSource::GetModPathsReturn
 DocumentSourceBidirectionalGraphLookup::getModifiedPaths() const {
     // No paths modified yet
     return {GetModPathsReturn::Type::kFiniteSet, {}, {}};
 }
 
 StageConstraints DocumentSourceBidirectionalGraphLookup::constraints(
     Pipeline::SplitState pipeState) const {
     
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
 
 boost::optional<DocumentSource::DistributedPlanLogic>
 DocumentSourceBidirectionalGraphLookup::distributedPlanLogic() {
     return boost::none;
 }
 
 boost::optional<ShardId>
 DocumentSourceBidirectionalGraphLookup::computeMergeShardId() const {
     return boost::none;
 }
 
 void DocumentSourceBidirectionalGraphLookup::detachFromOperationContext() {
     // No-op for now
 }
 
 void DocumentSourceBidirectionalGraphLookup::reattachToOperationContext(
     OperationContext* opCtx) {
     // No-op for now
 }
 
 }  // namespace mongo