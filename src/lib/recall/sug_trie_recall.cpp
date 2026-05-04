#include "lib/recall/sug_trie_recall.h"
#include "utils/logger.h"
#include <cmath>

namespace minisearchrec {

int SugTrieRecallProcessor::ProcessDag(framework::DagProcessorContext* ctx) {
    auto* ss = dynamic_cast<const SearchSession*>(ctx->session);
    if (!ss || ss->query.empty()) return 0;

    auto candidates = SugTrie::Instance().Search(ss->query, 50);
    if (candidates.empty()) { LOG_INFO("SugTrieRecall: no results for '{}'", ss->query); return 0; }

    float max_freq = 1.0f;
    for (const auto* e : candidates) max_freq = std::max(max_freq, (float)e->freq);
    int64_t now = std::time(nullptr);
    float prefix_len = (float)ss->query.size();

    std::vector<DocCandidate> docs;
    for (const auto* e : candidates) {
        DocCandidate cand;
        cand.doc_id = e->word;
        cand.recall_source = e->source;
        cand.recall_score = 0.0f;

        // 预存特征数据供 rank_stages 使用
        cand.click_count = e->freq;
        cand.publish_time = e->last_time;
        cand.quality_score = e->source_weight;
        float days = std::max(0.0f, (float)(now - e->last_time) / 86400.0f);
        float prefix_match = prefix_len / std::max(1.0f, (float)e->word.size());
        float freq_norm = std::log1pf((float)e->freq) / std::log1pf(max_freq);
        float freshness = std::exp(-0.099f * days);
        // 嵌入到 debug_scores 作为特征传递
        cand.debug_scores["pm"] = prefix_match;
        cand.debug_scores["fn"] = freq_norm;
        cand.debug_scores["fs"] = freshness;

        ctx->output->doc_scores.emplace_back(e->word, 0.0f);
        docs.push_back(std::move(cand));
    }

    ctx->output->items = std::move(docs);
    ctx->output->item_count = ctx->output->doc_scores.size();
    LOG_INFO("SugTrieRecall: prefix='{}', results={}", ss->query, ctx->output->item_count);
    return 0;
}

} // namespace minisearchrec

using namespace minisearchrec;
REGISTER_MSR_PROCESSOR(SugTrieRecallProcessor);
