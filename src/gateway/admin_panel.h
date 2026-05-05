// ============================================================
// MiniSearchRec - 管理后台 Dashboard（嵌入式 HTML）
// 单页面应用，通过 fetch() 调用 Admin API 渲染
// 零前端构建，启动即用
// ============================================================

#ifndef MINISEARCHREC_ADMIN_PANEL_H
#define MINISEARCHREC_ADMIN_PANEL_H

#include <string>

namespace minisearchrec {

const std::string kAdminHtml = R"html(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>MiniSearchRec Admin</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.7/dist/chart.umd.min.js"></script>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:#f5f6fa;color:#333;padding:20px}
h1{font-size:22px;margin-bottom:20px;color:#2c3e50}
h2{font-size:16px;margin-bottom:10px;color:#2c3e50}
.nav{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:20px}
.nav a{padding:8px 16px;background:#fff;border-radius:6px;text-decoration:none;color:#555;font-size:13px;cursor:pointer;border:1px solid #e0e0e0}
.nav a:hover,.nav a.active{background:#3498db;color:#fff;border-color:#3498db}
.section{display:none;background:#fff;border-radius:8px;padding:20px;margin-bottom:16px;box-shadow:0 1px 3px rgba(0,0,0,.08)}
.section.active{display:block}
.stats{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:12px}
.stat-card{background:#f8f9fa;border-radius:6px;padding:16px;text-align:center}
.stat-card .num{font-size:28px;font-weight:700;color:#2c3e50}
.stat-card .label{font-size:12px;color:#888;margin-top:4px}
.monitor-cards{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-bottom:16px}
.monitor-card{background:linear-gradient(135deg,#667eea,#764ba2);border-radius:8px;padding:20px;color:#fff}
.monitor-card:nth-child(2){background:linear-gradient(135deg,#f093fb,#f5576c)}
.monitor-card:nth-child(3){background:linear-gradient(135deg,#4facfe,#00f2fe)}
.monitor-card .m-num{font-size:32px;font-weight:700}
.monitor-card .m-label{font-size:13px;opacity:.85;margin-top:4px}
.monitor-card .m-sub{font-size:12px;opacity:.7;margin-top:2px}
.chart-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.chart-box{background:#f8f9fa;border-radius:6px;padding:12px}
.chart-box.full{grid-column:1/-1}
.chart-box h3{font-size:13px;color:#555;margin-bottom:8px}
.chart-box canvas{max-height:220px}
.biz-tabs a{padding:6px 14px;background:#fff;border-radius:4px;text-decoration:none;color:#555;font-size:13px;cursor:pointer;border:1px solid #e0e0e0}
.biz-tabs a:hover,.biz-tabs a.active{background:#3498db;color:#fff;border-color:#3498db}
.toolbar{display:flex;gap:8px;margin-bottom:12px;flex-wrap:wrap}
.toolbar input,.toolbar select{padding:6px 10px;border:1px solid #ddd;border-radius:4px;font-size:13px}
.toolbar button{padding:6px 14px;background:#3498db;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:13px}
.toolbar button:hover{background:#2980b9}
.toolbar button.danger{background:#e74c3c}
.toolbar button.danger:hover{background:#c0392b}
.toolbar button.success{background:#27ae60}
.toolbar button.success:hover{background:#219a52}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{padding:8px 10px;text-align:left;border-bottom:1px solid #eee;white-space:nowrap;max-width:300px;overflow:hidden;text-overflow:ellipsis}
th{background:#f8f9fa;font-weight:600;color:#555;position:sticky;top:0}
tr:hover{background:#f0f7ff}
.pagination{display:flex;gap:8px;align-items:center;margin-top:10px;font-size:13px}
.pagination button{padding:4px 10px;border:1px solid #ddd;border-radius:3px;background:#fff;cursor:pointer}
.pagination button:hover{background:#eee}
.empty{color:#999;font-size:13px;padding:20px 0}
.loading{color:#999;font-size:13px;padding:10px 0}
.error{color:#e74c3c;padding:10px;background:#fdf0ef;border-radius:4px;margin:8px 0;font-size:13px}
.tip{color:#999;font-size:12px;margin-bottom:8px}
.toggle-detail{color:#3498db;cursor:pointer;font-size:12px;margin-left:4px}
pre.json-display{background:#f8f9fa;padding:12px;border-radius:4px;overflow:auto;max-height:400px;font-size:12px;line-height:1.5}
</style>
</head>
<body>

<h1>🔍 MiniSearchRec 管理后台</h1>

<div class="nav">
  <a onclick="showSection('monitor')">数据看板</a>
  <a class="active" onclick="showSection('dashboard')">系统总览</a>
  <a onclick="showSection('docs')">文档管理</a>
  <a onclick="showSection('stats')">搜索词统计</a>
  <a onclick="showSection('trie')">SugTrie</a>
  <a onclick="showSection('cooccur')">共现数据</a>
  <a onclick="showSection('events')">事件日志</a>
  <a onclick="showSection('scheduler')">调度任务</a>
  <a onclick="showSection('abtest')">A/B 实验</a>
</div>

<!-- 数据看板（4 个 Biz 面板） -->
<div id="section-monitor" class="section">
  <h2>数据看板</h2>
  <div class="toolbar">
    <select id="monitor-hours" onchange="loadMonitor()">
      <option value="24">最近 24 小时</option>
      <option value="72">最近 3 天</option>
      <option value="168">最近 7 天</option>
      <option value="720">最近 30 天</option>
    </select>
    <select id="monitor-bucket" onchange="loadMonitor()">
      <option value="3600">按小时</option>
      <option value="86400">按天</option>
    </select>
  </div>
  <div class="biz-tabs" style="display:flex;gap:6px;margin-bottom:12px">
    <a class="active" onclick="switchBizTab('search')">🔍 搜索</a>
    <a onclick="switchBizTab('sug')">💡 建议</a>
    <a onclick="switchBizTab('hint')">🔗 推荐</a>
    <a onclick="switchBizTab('nav')">🧭 导航</a>
  </div>
  <div class="monitor-cards" id="monitor-cards">
    <div class="monitor-card"><div class="m-num">-</div><div class="m-label">曝光量</div><div class="m-sub">搜索结果数</div></div>
    <div class="monitor-card"><div class="m-num">-</div><div class="m-label">点击量</div><div class="m-sub">用户点击次数</div></div>
    <div class="monitor-card"><div class="m-num">-</div><div class="m-label">曝光点击率 (CTR)</div><div class="m-sub">点击 / 曝光</div></div>
  </div>
  <div class="chart-grid">
    <div class="chart-box full"><h3>曝光与点击趋势</h3><canvas id="chart-main"></canvas></div>
    <div class="chart-box"><h3>点击量趋势</h3><canvas id="chart-clicks"></canvas></div>
    <div class="chart-box"><h3>CTR 趋势</h3><canvas id="chart-ctr"></canvas></div>
  </div>
</div>

<!-- 系统总览 -->
<div id="section-dashboard" class="section active">
  <h2>系统总览</h2>
  <div class="stats" id="dashboard-stats"><div class="loading">加载中...</div></div>
</div>

<!-- 文档管理 -->
<div id="section-docs" class="section">
  <h2>文档管理</h2>
  <div class="toolbar">
    <input type="text" id="doc-list-page-size" value="10" placeholder="每页条数" style="width:80px">
    <button onclick="loadDocs(1)">刷新</button>
  </div>
  <table><thead><tr><th>ID</th><th>标题</th><th>分类</th><th>作者</th><th>点击数</th><th>点赞</th><th>质量分</th><th>操作</th></tr></thead><tbody id="doc-tbody"></tbody></table>
  <div class="pagination" id="doc-pagination"></div>
  <div id="doc-detail" style="display:none;margin-top:12px">
    <h3>文档详情 <span id="doc-detail-id" style="font-weight:normal;color:#888"></span></h3>
    <pre class="json-display" id="doc-detail-json"></pre>
    <button onclick="document.getElementById('doc-detail').style.display='none'" style="margin-top:8px;padding:4px 12px;border:1px solid #ddd;border-radius:3px;cursor:pointer">关闭</button>
  </div>
</div>

<!-- 搜索词统计 -->
<div id="section-stats" class="section">
  <h2>搜索词统计</h2>
  <div class="toolbar">
    <input type="text" id="stats-prefix" placeholder="前缀（空=热门词）" style="width:180px">
    <input type="number" id="stats-top" value="10" style="width:70px">
    <button onclick="loadStats()">查询</button>
  </div>
  <table><thead><tr><th>查询词</th><th>频率</th><th>最后时间</th><th>来源</th></tr></thead><tbody id="stats-tbody"></tbody></table>
</div>

<!-- SugTrie -->
<div id="section-trie" class="section">
  <h2>SugTrie 索引内容</h2>
  <div class="toolbar">
    <input type="text" id="trie-prefix" placeholder="前缀（必填）" style="width:180px">
    <input type="number" id="trie-limit" value="20" style="width:70px">
    <button onclick="loadTrie()">查询</button>
    <button class="success" onclick="rebuildTrie()">重建 Trie</button>
  </div>
  <table><thead><tr><th>词</th><th>来源</th><th>频率</th><th>最后时间</th><th>权重</th></tr></thead><tbody id="trie-tbody"></tbody></table>
</div>

<!-- 共现数据 / 行为链 -->
<div id="section-cooccur" class="section">
  <h2>行为链与共现数据</h2>
  <div class="stats" style="grid-template-columns:1fr 1fr;margin-bottom:12px">
    <div style="background:#f8f9fa;border-radius:6px;padding:12px">
      <strong style="font-size:13px;color:#555">行为链</strong>
      <div class="toolbar" style="margin-top:8px;margin-bottom:0">
        <input type="text" id="cooccur-uid" placeholder="uid（查看该用户点击序列）" style="width:220px">
        <button onclick="loadBehaviorChain()">查看链</button>
      </div>
    </div>
    <div style="background:#f8f9fa;border-radius:6px;padding:12px">
      <strong style="font-size:13px;color:#555">共现查询</strong>
      <div class="toolbar" style="margin-top:8px;margin-bottom:0">
        <input type="text" id="cooccur-doc-id" placeholder="doc_id（该文档与哪些文档共现）" style="width:220px">
        <input type="number" id="cooccur-top" value="10" style="width:60px">
        <button onclick="loadCooccur()">查询</button>
      </div>
    </div>
  </div>
  <div id="cooccur-summary"></div>
  <div id="chain-display"></div>
  <table id="cooccur-table" style="display:none"><thead><tr><th>目标文档</th><th>共现次数</th><th>最后时间</th></tr></thead><tbody id="cooccur-tbody"></tbody></table>
</div>

<!-- 事件日志 -->
<div id="section-events" class="section">
  <h2>事件日志</h2>
  <div class="toolbar">
    <input type="text" id="event-uid" placeholder="uid（可空）" style="width:150px">
    <select id="event-type">
      <option value="">全部类型</option>
      <option value="click">click</option>
      <option value="like">like</option>
      <option value="share">share</option>
      <option value="dismiss">dismiss</option>
    </select>
    <input type="number" id="event-limit" value="20" style="width:70px">
    <button onclick="loadEvents()">查询</button>
  </div>
  <table><thead><tr><th>UID</th><th>文档</th><th>类型</th><th>搜索词</th><th>位置</th><th>时长(ms)</th><th>时间</th></tr></thead><tbody id="event-tbody"></tbody></table>
</div>

<!-- 调度任务 -->
<div id="section-scheduler" class="section">
  <h2>调度任务</h2>
  <div id="scheduler-status" class="loading">加载中...</div>
  <div class="toolbar" style="margin-top:12px">
    <button class="danger" onclick="triggerRebuild('index')">触发索引重建</button>
    <button class="success" onclick="triggerRebuild('trie')">触发 Trie 重建</button>
  </div>
</div>

<!-- A/B 实验 -->
<div id="section-abtest" class="section">
  <h2>A/B 实验</h2>
  <div class="toolbar">
    <input type="text" id="abtest-uid" placeholder="uid（查看分组）" style="width:180px">
    <button onclick="loadABTest()">查询</button>
  </div>
  <div id="abtest-result" class="loading">加载中...</div>
</div>

<script>
const BASE = '';

function showSection(name) {
  document.querySelectorAll('.section').forEach(s => s.classList.remove('active'));
  document.querySelectorAll('.nav a').forEach(a => a.classList.remove('active'));
  document.getElementById('section-' + name).classList.add('active');
  document.querySelector(`.nav a[onclick*="'${name}'"]`).classList.add('active');
  // 切换时懒加载对应数据
  switch(name) {
    case 'monitor': loadMonitor(); break;
    case 'docs': loadDocs(1); break;
    case 'stats': loadStats(); break;
    case 'trie': break;
    case 'cooccur': loadCooccur(); break;
    case 'events': loadEvents(); break;
    case 'scheduler': loadScheduler(); break;
    case 'abtest': loadABTest(); break;
  }
}

// 时间格式化
function fmtTs(ts) {
  if (!ts) return '-';
  return new Date(ts * 1000).toLocaleString('zh-CN');
}

// ===== 系统总览 =====
function loadDashboard() {
  fetch(BASE + '/api/v1/admin/dashboard')
    .then(r => r.json())
    .then(d => {
      if (d.ret !== 0) { document.getElementById('dashboard-stats').innerHTML = '<div class="error">' + d.err_msg + '</div>'; return; }
      const data = d.data;
      const cards = [
        { label: '文档数', num: data.doc_count },
        { label: '倒排词数', num: data.term_count },
        { label: '向量数', num: data.vector_count },
        { label: '搜索词统计', num: data.query_stats_count },
        { label: '共现记录', num: data.cooccur_count },
        { label: 'SugTrie 词条', num: data.sug_trie_size },
        { label: '系统状态', num: data.ready ? '✓ 就绪' : '未就绪' }
      ];
      document.getElementById('dashboard-stats').innerHTML = cards.map(c =>
        `<div class="stat-card"><div class="num">${c.num}</div><div class="label">${c.label}</div></div>`
      ).join('');
    })
    .catch(e => document.getElementById('dashboard-stats').innerHTML = '<div class="error">请求失败: ' + e.message + '</div>');
}

// ===== 文档管理 =====
let docPage = 1, docTotal = 0;
function loadDocs(page) {
  docPage = page || 1;
  const ps = document.getElementById('doc-list-page-size').value || 10;
  fetch(BASE + '/api/v1/doc/list?page=' + docPage + '&page_size=' + ps)
    .then(r => r.json())
    .then(d => {
      if (d.ret !== 0) { document.getElementById('doc-tbody').innerHTML = '<tr><td colspan="8" class="error">' + d.err_msg + '</td></tr>'; return; }
      const data = d.data;
      docTotal = data.total;
      const tbody = document.getElementById('doc-tbody');
      tbody.innerHTML = (data.items || []).map(doc =>
        `<tr>
          <td>${esc(doc.doc_id)}</td>
          <td title="${esc(doc.title)}">${esc(trunc(doc.title, 40))}</td>
          <td>${esc(doc.category || '-')}</td>
          <td>${esc(doc.author || '-')}</td>
          <td>${doc.click_count || 0}</td>
          <td>${doc.like_count || 0}</td>
          <td>${(doc.quality_score || 0).toFixed(3)}</td>
          <td><span class="toggle-detail" onclick="showDocDetail('${esc(doc.doc_id)}')">详情</span></td>
        </tr>`
      ).join('');
      const totalPages = Math.ceil(data.total / parseInt(ps));
      document.getElementById('doc-pagination').innerHTML =
        `<button onclick="loadDocs(${docPage-1})" ${docPage<=1?'disabled':''}>上一页</button>` +
        `<span>第 ${docPage}/${totalPages || 1} 页，共 ${data.total} 条</span>` +
        `<button onclick="loadDocs(${docPage+1})" ${docPage>=totalPages?'disabled':''}>下一页</button>`;
    });
}

function showDocDetail(docId) {
  fetch(BASE + '/api/v1/doc/get?doc_id=' + encodeURIComponent(docId))
    .then(r => r.json())
    .then(d => {
      const el = document.getElementById('doc-detail');
      document.getElementById('doc-detail-id').textContent = ' - ' + docId;
      document.getElementById('doc-detail-json').textContent = JSON.stringify(d.data, null, 2);
      el.style.display = 'block';
    });
}

// ===== 搜索词统计 =====
function loadStats() {
  const prefix = document.getElementById('stats-prefix').value;
  const top = document.getElementById('stats-top').value || 10;
  let url = BASE + '/api/v1/admin/stats/query?top=' + top;
  if (prefix) url += '&prefix=' + encodeURIComponent(prefix);
  fetch(url).then(r => r.json()).then(d => {
    const tbody = document.getElementById('stats-tbody');
    if (d.ret !== 0) { tbody.innerHTML = '<tr><td colspan="4" class="error">' + d.err_msg + '</td></tr>'; return; }
    tbody.innerHTML = (d.data.items || []).map(item =>
      `<tr><td>${esc(item.query)}</td><td>${item.freq}</td><td>${fmtTs(item.last_time)}</td><td>${esc(item.source)}</td></tr>`
    ).join('');
    if (!d.data.items || !d.data.items.length) tbody.innerHTML = '<tr><td colspan="4" class="empty">无数据</td></tr>';
  });
}

// ===== SugTrie =====
function loadTrie() {
  const prefix = document.getElementById('trie-prefix').value;
  const limit = document.getElementById('trie-limit').value || 20;
  if (!prefix) { alert('请输入前缀'); return; }
  fetch(BASE + '/api/v1/admin/sug/trie?prefix=' + encodeURIComponent(prefix) + '&limit=' + limit)
    .then(r => r.json()).then(d => {
      const tbody = document.getElementById('trie-tbody');
      if (d.ret !== 0) { tbody.innerHTML = '<tr><td colspan="5" class="error">' + d.err_msg + '</td></tr>'; return; }
      tbody.innerHTML = (d.data.items || []).map(item =>
        `<tr><td>${esc(item.word)}</td><td>${esc(item.source)}</td><td>${item.freq}</td><td>${fmtTs(item.last_time)}</td><td>${item.source_weight.toFixed(3)}</td></tr>`
      ).join('');
      if (!d.data.items || !d.data.items.length) tbody.innerHTML = '<tr><td colspan="5" class="empty">无匹配</td></tr>';
    });
}

function rebuildTrie() {
  if (!confirm('确认重建 SugTrie？')) return;
  fetch(BASE + '/api/v1/admin/sug/trie/rebuild', { method: 'POST' })
    .then(r => r.json()).then(d => {
      alert(d.ret === 0 ? 'Trie 重建成功' : '重建失败: ' + d.err_msg);
      loadTrie();
    });
}

// ===== 行为链与共现数据 =====
let cooccurLoaded = false;
function loadCooccur() {
  const docId = document.getElementById('cooccur-doc-id').value;
  const top = document.getElementById('cooccur-top').value || 10;
  
  if (!cooccurLoaded) {
    cooccurLoaded = true;
    fetch(BASE + '/api/v1/admin/dashboard')
      .then(r => r.json()).then(d => {
        if (d.ret === 0 && d.data) {
          document.getElementById('cooccur-summary').innerHTML =
            `<div class="tip">共 ${d.data.cooccur_count} 条共现记录。行为链来源于用户点击事件日志。</div>`;
        }
      });
  }
  
  if (!docId) {
    document.getElementById('cooccur-table').style.display = 'none';
    return;
  }
  
  fetch(BASE + '/api/v1/admin/stats/cooccur?doc_id=' + encodeURIComponent(docId) + '&top=' + top)
    .then(r => r.json()).then(d => {
      const table = document.getElementById('cooccur-table');
      const tbody = document.getElementById('cooccur-tbody');
      table.style.display = '';
      if (d.ret !== 0) { tbody.innerHTML = '<tr><td colspan="3" class="error">' + d.err_msg + '</td></tr>'; return; }
      tbody.innerHTML = (d.data.items || []).map(item =>
        `<tr><td>${esc(item.dst_doc_id)}</td><td>${item.co_count}</td><td>${fmtTs(item.last_time)}</td></tr>`
      ).join('');
      if (!d.data.items || !d.data.items.length) tbody.innerHTML = '<tr><td colspan="3" class="empty">该文档暂无共现数据</td></tr>';
    });
}

function loadBehaviorChain() {
  const uid = document.getElementById('cooccur-uid').value;
  if (!uid) { document.getElementById('chain-display').innerHTML = '<div class="tip">输入 uid 查看该用户的点击序列</div>'; return; }
  
  document.getElementById('chain-display').innerHTML = '<div class="loading">加载行为链...</div>';
  document.getElementById('cooccur-table').style.display = 'none';
  
  fetch(BASE + '/api/v1/admin/events?uid=' + encodeURIComponent(uid) + '&event_type=click&limit=100')
    .then(r => r.json()).then(d => {
      if (d.ret !== 0) { document.getElementById('chain-display').innerHTML = '<div class="error">' + d.err_msg + '</div>'; return; }
      const items = d.data.items || [];
      if (!items.length) { document.getElementById('chain-display').innerHTML = '<div class="empty">该用户暂无点击事件</div>'; return; }
      
      items.sort((a, b) => a.ts - b.ts);
      
      // 行为链渲染：doc →→ doc →→ doc
      let html = `<div class="tip">用户 <strong>${esc(uid)}</strong> 的点击行为链（${items.length} 次）：</div>`;
      html += '<div style="display:flex;flex-wrap:wrap;align-items:center;gap:4px;padding:12px;background:#f8f9fa;border-radius:6px;overflow-x:auto;min-height:40px">';
      
      items.forEach((item, i) => {
        html += `<span style="display:inline-flex;align-items:center;gap:4px;background:#fff;border:1px solid #e0e0e0;border-radius:4px;padding:4px 10px;font-size:12px;white-space:nowrap" title="${fmtTs(item.ts)}">`;
        html += esc(item.doc_id);
        html += `<span style="color:#999;font-size:10px">${fmtTs(item.ts).split(' ')[1]||''}</span>`;
        html += '</span>';
        if (i < items.length - 1) {
          html += '<span style="color:#3498db;font-weight:bold;font-size:16px">→</span>';
          // 如果两跳之间跨搜索（query 变了），加标注
          if (item.query !== items[i+1].query) {
            html += `<span style="color:#999;font-size:10px">[${esc(item.query||'-')}→${esc(items[i+1].query||'-')}]</span>`;
          }
        }
      });
      html += '</div>';
      
      // 标注共现对
      const pairs = [];
      for (let i = 0; i < items.length - 1; i++) {
        if (items[i].doc_id !== items[i+1].doc_id) {
          pairs.push(`${items[i].doc_id} → ${items[i+1].doc_id}`);
        }
      }
      if (pairs.length) {
        html += `<div style="margin-top:8px;font-size:12px;color:#888">此序列产生了 ${pairs.length} 个共现对：${pairs.join('；')}</div>`;
      }
      
      document.getElementById('chain-display').innerHTML = html;
    });
}

// ===== 事件日志 =====
function loadEvents() {
  const uid = document.getElementById('event-uid').value;
  const et = document.getElementById('event-type').value;
  const limit = document.getElementById('event-limit').value || 20;
  let url = BASE + '/api/v1/admin/events?limit=' + limit;
  if (uid) url += '&uid=' + encodeURIComponent(uid);
  if (et) url += '&event_type=' + et;
  fetch(url).then(r => r.json()).then(d => {
    const tbody = document.getElementById('event-tbody');
    if (d.ret !== 0) { tbody.innerHTML = '<tr><td colspan="7" class="error">' + d.err_msg + '</td></tr>'; return; }
    tbody.innerHTML = (d.data.items || []).map(item =>
      `<tr><td>${esc(item.uid)}</td><td>${esc(item.doc_id)}</td><td>${esc(item.event_type)}</td><td>${esc(item.query||'-')}</td><td>${item.result_pos}</td><td>${item.duration_ms}</td><td>${fmtTs(item.ts)}</td></tr>`
    ).join('');
    if (!d.data.items || !d.data.items.length) tbody.innerHTML = '<tr><td colspan="7" class="empty">无事件</td></tr>';
  });
}

// ===== 调度任务 =====
function loadScheduler() {
  fetch(BASE + '/api/v1/admin/scheduler/status')
    .then(r => r.json()).then(d => {
      const el = document.getElementById('scheduler-status');
      if (d.ret !== 0) { el.innerHTML = '<div class="error">' + d.err_msg + '</div>'; return; }
      const data = d.data;
      el.innerHTML = `<p style="margin-bottom:8px">调度器运行中: <strong>${data.running ? '✓ 是' : '✗ 否'}</strong></p>`;
      if (data.items && data.items.length) {
        let html = '<table><thead><tr><th>任务名</th><th>启用</th><th>间隔(秒)</th><th>上次运行</th></tr></thead><tbody>';
        data.items.forEach(t => {
          html += `<tr><td>${esc(t.name)}</td><td>${t.enabled ? '✓' : '✗'}</td><td>${t.interval_sec}</td><td>${t.last_run_epoch ? fmtTs(t.last_run_epoch) : '尚未运行'}</td></tr>`;
        });
        html += '</tbody></table>';
        el.innerHTML += html;
      }
    });
}

function triggerRebuild(type) {
  const names = { index: '索引', trie: 'SugTrie' };
  if (!confirm(`确认触发 ${names[type]} 全量重建？`)) return;
  const url = type === 'index' ? '/api/v1/admin/index/rebuild' : '/api/v1/admin/sug/trie/rebuild';
  fetch(BASE + url, { method: 'POST' })
    .then(r => r.json()).then(d => {
      alert(d.ret === 0 ? names[type] + ' 重建成功' : '重建失败: ' + d.err_msg);
      loadScheduler();
    });
}

// ===== A/B 实验 =====
function loadABTest() {
  const uid = document.getElementById('abtest-uid').value;
  let url = BASE + '/api/v1/admin/abtest';
  if (uid) url += '?uid=' + encodeURIComponent(uid);
  fetch(url).then(r => r.json()).then(d => {
    const el = document.getElementById('abtest-result');
    if (d.ret !== 0) { el.innerHTML = '<div class="error">' + d.err_msg + '</div>'; return; }
    const data = d.data;
    if (!data.experiments || !data.experiments.length) {
      el.innerHTML = '<div class="tip">暂无配置实验</div>';
      if (data.assigned_to) el.innerHTML += `<p>用户分组: <strong>${esc(data.assigned_to)}</strong></p>`;
      return;
    }
    let html = data.assigned_to ? `<p>用户分组: <strong>${esc(data.assigned_to)}</strong></p>` : '';
    html += '<table><thead><tr><th>实验名</th><th>流量占比</th><th>分桶方法</th><th>参数</th></tr></thead><tbody>';
    data.experiments.forEach(exp => {
      let paramsHtml = '';
      if (exp.params) {
        paramsHtml = Object.entries(exp.params).map(([k,v]) => `${k}=${v}`).join('<br>');
      }
      html += `<tr><td>${esc(exp.name)}</td><td>${(exp.traffic_ratio * 100).toFixed(1)}%</td><td>${esc(exp.bucket_method)}</td><td>${paramsHtml || '-'}</td></tr>`;
    });
    html += '</tbody></table>';
    el.innerHTML = html;
  });
}

function esc(s) { return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }
function trunc(s, n) { return s && s.length > n ? s.slice(0, n) + '...' : s; }

// ===== 数据看板（4 Biz 面板）=====
let monitorCharts = {};
let currentBiz = 'search';

function switchBizTab(biz) {
  currentBiz = biz;
  document.querySelectorAll('.biz-tabs a').forEach(a => a.classList.remove('active'));
  document.querySelector(`.biz-tabs a[onclick*="'${biz}'"]`).classList.add('active');
  loadMonitor();
}

function loadMonitor() {
  const hours = document.getElementById('monitor-hours').value;
  const bucket = document.getElementById('monitor-bucket').value;
  
  fetch(BASE + `/api/v1/admin/report/timeseries?biz=${currentBiz}&hours=${hours}&bucket=${bucket}`)
    .then(r => r.json()).then(d => {
      if (d.ret !== 0) return;
      const data = d.data;
      const sum = data.summary;
      
      // 更新指标卡片
      document.querySelector('#monitor-cards .monitor-card:nth-child(1) .m-num').textContent = sum.total_impressions.toLocaleString();
      document.querySelector('#monitor-cards .monitor-card:nth-child(2) .m-num').textContent = sum.total_clicks.toLocaleString();
      document.querySelector('#monitor-cards .monitor-card:nth-child(3) .m-num').textContent = (sum.ctr * 100).toFixed(2) + '%';
      
      // 图表数据
      const labels = (data.items || []).map(item => {
        const d = new Date(item.ts * 1000);
        return bucket == '86400' ? d.toLocaleDateString('zh-CN') : d.toLocaleString('zh-CN', {hour:'2-digit',minute:'2-digit'});
      });
      const impressions = (data.items || []).map(item => item.impression);
      const clicks = (data.items || []).map(item => item.click);
      const ctrs   = (data.items || []).map(item => item.impression > 0 ? (item.click / item.impression * 100) : 0);
      
      // 销毁旧图表
      Object.values(monitorCharts).forEach(c => { try { c.destroy(); } catch(e) {} });
      monitorCharts = {};
      
      const makeChart = (id, label, dataArr, color) => new Chart(document.getElementById(id), {
        type: 'line',
        data: {
          labels,
          datasets: [
            ...(dataArr.length > 1 ? [{ label: '曝光', data: impressions, borderColor: '#667eea', backgroundColor: '#667eea20', yAxisID: 'y' }] : []),
            { label, data: dataArr, borderColor: color, backgroundColor: color + '20', fill: true, tension: 0.3, pointRadius: 2, yAxisID: label.includes('CTR') ? 'y1' : 'y' }
          ]
        },
        options: {
          responsive: true, maintainAspectRatio: false,
          plugins: { legend: { display: false } },
          scales: {
            x: { ticks: { maxTicksLimit: 12, font: { size: 10 } } },
            y: { beginAtZero: true, ticks: { font: { size: 10 } } },
            ...(label.includes('CTR') ? { y1: { beginAtZero: true, max: 100, position: 'right', ticks: { font: { size: 10 }, callback: v => v + '%' } } } : {})
          }
        }
      });
      
      // 主图显示曝光+点击双线
      if (impressions.length > 0) {
        const mainCtx = document.getElementById('chart-main');
        monitorCharts.main = new Chart(mainCtx, {
          type: 'line',
          data: {
            labels,
            datasets: [
              { label: '曝光', data: impressions, borderColor: '#667eea', backgroundColor: '#667eea20', fill: true, tension: 0.3, pointRadius: 2 },
              { label: '点击', data: clicks, borderColor: '#f5576c', backgroundColor: '#f5576c20', fill: true, tension: 0.3, pointRadius: 2 }
            ]
          },
          options: {
            responsive: true, maintainAspectRatio: false,
            plugins: { legend: { display: true, position: 'top', labels: { font: { size: 11 } } } },
            scales: { x: { ticks: { maxTicksLimit: 12, font: { size: 10 } } }, y: { beginAtZero: true, ticks: { font: { size: 10 } } } }
          }
        });
      }
      monitorCharts.clicks = makeChart('chart-clicks', '点击', clicks, '#f5576c');
      monitorCharts.ctr    = makeChart('chart-ctr', 'CTR %', ctrs, '#4facfe');
    });
}

// 初始化
loadDashboard();
setInterval(loadDashboard, 30000);
</script>

</body>
</html>
)html";

} // namespace minisearchrec

#endif // MINISEARCHREC_ADMIN_PANEL_H
