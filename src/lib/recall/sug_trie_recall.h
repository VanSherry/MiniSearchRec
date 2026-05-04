#ifndef MINISEARCHREC_SUG_TRIE_RECALL_H
#define MINISEARCHREC_SUG_TRIE_RECALL_H

#include "framework/processor/dag_pipeline.h"
#include "biz/search/search_session.h"
#include "biz/sug/sug_trie.h"

namespace minisearchrec {

// Sug Trie 前缀召回（DAG 并行模式）
class SugTrieRecallProcessor : public BaseRecallProcessor {
public:
    int ProcessDag(framework::DagProcessorContext* ctx) override;
    std::string Name() const override { return "SugTrieRecallProcessor"; }
    int Init(const YAML::Node& config) override { return 0; }
};

} // namespace minisearchrec

#endif
