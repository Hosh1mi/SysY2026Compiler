#!/usr/bin/env node
// 依赖无关版本：用 Node 内置 fetch + 正则解析，去掉 axios / cheerio。
// Node >= 23.6 可直接 `node test/scoreboard.ts` 运行（原生 TS type-stripping）。
//
// 用法：
//   node test/scoreboard.ts fetch [arm|riscv]   抓取排行榜并存到 test/results/rank/
//   node test/scoreboard.ts <n>                 比较 rank/<n>.txt 与 rank/<n+1>.txt
//   node test/scoreboard.ts <f1> <f2>           比较两个文件
import * as fs from 'fs';
import * as path from 'path';
import * as readline from 'readline';

interface ScoreEntry {
  name: string;
  time: number;
}

// 准确的抓取参数（已核验）：
//   contestID = 比赛 ID；taskID 区分赛道——ARM(A53) 与 RISCV 两条赛道；
//   排行榜数据真正的端点是 contest_rank_load.jsp（POST），
//   contest_rank.jsp 只是壳、由 JS 再 POST 拉数据。
const contest_id = "Zd6BcaiCWJ4";
const task_ids: Record<string, string> = {
  arm: "7123352",    // 2026 编译系统实现赛初赛 - ARM 赛道（A53 评测平台）
  riscv: "7090546",  // RISCV 赛道
};
const origin = "https://course.educg.net";
const load_endpoint = `${origin}/pages/contest/contest_rank_load.jsp`;

interface Row {
  rank: string; username: string; team: string; school: string;
  submissions: string; lastSubmit: string; correctness: string;
  performance: string; total: string;
}

function decodeEntities(s: string): string {
  return s.replace(/&amp;/g, "&").replace(/&lt;/g, "<").replace(/&gt;/g, ">")
          .replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&nbsp;/g, " ");
}

function parseRows(html: string): Row[] {
  const out: Row[] = [];
  const trs = html.match(/<tr[\s\S]*?<\/tr>/gi) || [];
  for (const tr of trs) {
    if (/table-info/i.test(tr)) continue;            // 跳过表头
    const rankM = tr.match(/<th[^>]*>([\s\S]*?)<\/th>/i);
    const cells = [...tr.matchAll(/<td[^>]*>([\s\S]*?)<\/td>/gi)]
                    .map(m => decodeEntities(m[1].replace(/<[^>]+>/g, "").trim()));
    if (!rankM || cells.length < 8) continue;
    out.push({
      rank: rankM[1].replace(/<[^>]+>/g, "").trim(),
      username: cells[0], team: cells[1], school: cells[2],
      submissions: cells[3], lastSubmit: cells[4], correctness: cells[5],
      performance: cells[6], total: cells[7],
    });
  }
  return out;
}

async function fetchBoard(track: string) {
  const taskID = task_ids[track];
  if (!taskID) { console.error(`unknown track: ${track} (use arm|riscv)`); return; }

  const res = await fetch(load_endpoint, {
    method: "POST",
    headers: {
      "Content-Type": "application/x-www-form-urlencoded",
      "User-Agent": "Mozilla/5.0",
      "Referer": `${origin}/pages/contest/contest_rank.jsp?contestID=${contest_id}&taskID=${taskID}`,
    },
    body: `contestID=${contest_id}&taskID=${taskID}`,
  });
  const html = await res.text();
  const rows = parseRows(html);
  if (rows.length === 0) {
    console.error(`no rows parsed (status ${res.status}, len ${html.length}). 端点/参数可能有变。`);
    return;
  }

  // TSV：用制表符分隔（最后提交时间含空格，不能用空格分隔）。
  const header = ["rank","username","team","school","submissions","last_submit","correctness","performance","total"];
  const lines = [header.join("\t"),
    ...rows.map(r => [r.rank, r.username, r.team, r.school, r.submissions,
                      r.lastSubmit, r.correctness, r.performance, r.total].join("\t"))];

  const outDir = path.join(import.meta.dirname, "results", "rank");
  fs.mkdirSync(outDir, { recursive: true });
  const date = new Date().toISOString().slice(0, 10);
  const outFile = path.join(outDir, `${track}_${date}.txt`);
  fs.writeFileSync(outFile, lines.join("\n") + "\n");

  console.log(`saved ${rows.length} rows -> ${outFile}`);
  const mine = rows.find(r => /NO_COMPILE_NO_LIFE/i.test(r.team));
  if (mine)
    console.log(`our team: #${mine.rank} ${mine.team} perf=${mine.performance} total=${mine.total} (subs=${mine.submissions})`);
  console.log("\ntop 5:");
  for (const r of rows.slice(0, 5))
    console.log(`  #${r.rank.padStart(2)} ${r.team.padEnd(24)} perf=${r.performance} total=${r.total}`);
}

// 排行榜详情（每队各测试点真实时间）：contest_rank_more.jsp。
// 该页需要登录态——匿名请求服务端 NPE（系统异常）。Cookie 从环境变量
// EDUCG_COOKIE 读取（不写进文件、不入库），形如浏览器 DevTools 里
// contest_rank_more.jsp 请求头的整段 Cookie 值。
async function fetchDetail(track: string) {
  const taskID = task_ids[track];
  if (!taskID) { console.error(`unknown track: ${track}`); return; }
  const cookie = process.env.EDUCG_COOKIE;
  if (!cookie) {
    console.error("缺少 EDUCG_COOKIE 环境变量。请登录后从浏览器复制 Cookie：");
    console.error("  EDUCG_COOKIE='educg_session=...; ...' node test/scoreboard.ts detail arm");
    return;
  }
  const url = `${origin}/pages/contest/contest_rank_more.jsp?contestID=${contest_id}&taskID=${taskID}`;
  const res = await fetch(url, {
    headers: {
      "User-Agent": "Mozilla/5.0", "Cookie": cookie,
      "Referer": `${origin}/pages/contest/contest_rank.jsp?contestID=${contest_id}&taskID=${taskID}`,
    },
  });
  const html = await res.text();
  if (/系统异常/.test(html) || (html.match(/<table/gi) || []).length === 0) {
    console.error(`详情抓取失败（status ${res.status}）。Cookie 可能未登录/已过期，或参数有变。`);
    return;
  }
  const outDir = path.join(import.meta.dirname, "results", "rank");
  fs.mkdirSync(outDir, { recursive: true });
  const date = new Date().toISOString().slice(0, 10);
  const rawFile = path.join(outDir, `${track}_detail_${date}.html`);
  fs.writeFileSync(rawFile, html);

  // 详情表（登录态、本队视角）每行单元格：
  //   rank, 测试点, 是否通过, 本队运行时间, 最佳性能时间, 最佳队伍, 最佳队伍学校
  const trs = html.match(/<tr[\s\S]*?<\/tr>/gi) || [];
  const header = ["testpoint","pass","your_time","best_time","best_team","best_school"];
  const out: string[] = [header.join("\t")];
  for (const tr of trs) {
    if (/<th[^>]*scope="col"/i.test(tr)) continue;  // 跳过表头
    const cells = [...tr.matchAll(/<t[hd][^>]*>([\s\S]*?)<\/t[hd]>/gi)]
      .map(m => decodeEntities(m[1].replace(/<[^>]+>/g, " ").replace(/\s+/g, " ").trim()));
    if (cells.length < 6) continue;
    // cells: [rank, testpoint, pass, your_time, best_time, best_team, best_school?]
    out.push([cells[1], cells[2], cells[3], cells[4], cells[5], cells[6] || ""].join("\t"));
  }
  const tsvFile = path.join(outDir, `${track}_detail_${date}.txt`);
  fs.writeFileSync(tsvFile, out.join("\n") + "\n");

  // 顺带算出与最佳的差距，按倍数降序提示最该优化的测试点
  const gaps = out.slice(1).map(l => {
    const c = l.split("\t");
    const you = parseFloat(c[2]), best = parseFloat(c[3]);
    return { tp: c[0], you, best, ratio: best > 0 ? you / best : (you > 0 ? Infinity : 1) };
  });
  console.log(`saved detail (${gaps.length} testpoints) -> ${tsvFile}\n  raw html -> ${rawFile}`);
  const behind = gaps.filter(g => g.ratio > 1.5).sort((a, b) => b.ratio - a.ratio);
  console.log(`\nbiggest gaps vs best (${behind.length} testpoints > 1.5x slower):`);
  for (const g of behind.slice(0, 12))
    console.log(`  ${g.tp.padEnd(20)} you=${g.you.toFixed(2).padStart(7)}  best=${g.best.toFixed(2).padStart(6)}  ${isFinite(g.ratio) ? g.ratio.toFixed(1) + "x" : "best=0"}`);
  const wins = gaps.filter(g => g.you <= g.best + 1e-9 && g.you > 0);
  console.log(`\nour team holds best on: ${wins.map(g => g.tp).join(", ") || "(none)"}`);
}

function parseLine(line: string): ScoreEntry | null {
  const parts = line.trim().split(/\s+/);
  if (parts.length < 6) return null;
  return { name: parts[1], time: parseFloat(parts[3]) };
}

function parseTest(line: string): ScoreEntry | null {
  const parts = line.trim().split(/\s+/);
  if (parts.length < 2) return null;
  return { name: parts[0], time: parseFloat(parts[1]) };
}

async function read(filePath: string, parser: (s: string) => ScoreEntry | null): Promise<ScoreEntry[]> {
  const fileStream = fs.createReadStream(filePath);
  const rl = readline.createInterface({ input: fileStream, crlfDelay: Infinity });
  const entries: ScoreEntry[] = [];
  for await (const line of rl) {
    const parsed = parser(line);
    if (parsed) entries.push(parsed);
  }
  return entries;
}

function compare(f1: ScoreEntry[], f2: ScoreEntry[], threshold = 1) {
  console.log("Significant changes:");
  const namelen = f1.map((x) => x.name.length).reduce((x, cur) => Math.max(x, cur));
  f1.forEach((a, i) => {
    const b = f2[i];
    const delta = (b.time - a.time) / a.time * 100;
    if (Math.abs(delta) >= threshold) {
      const plus = delta > 0 ? "+" : "";
      const change = `${a.time.toFixed(2)} -> ${b.time.toFixed(2)}`.padEnd(16);
      console.log(`${a.name.padEnd(namelen + 1)} ${change} (${plus}${delta.toFixed(2)}%)`);
    }
  });
}

async function main() {
  const argv = process.argv;
  const len = argv.length;

  if (len >= 3 && argv[2] === "fetch") {
    await fetchBoard(argv[3] || "arm");
    return;
  }
  if (len >= 3 && argv[2] === "detail") {
    await fetchDetail(argv[3] || "arm");
    return;
  }

  let name1: string, name2: string;
  if (len < 3) {
    console.log("usage:\n  node test/scoreboard.ts fetch [arm|riscv]\n  node test/scoreboard.ts <n>\n  node test/scoreboard.ts <f1> <f2>");
    return;
  }
  if (len == 3) {
    const count = parseInt(argv[2]);
    name1 = count.toString();
    name2 = (count + 1).toString();
  } else if (len == 4) {
    name1 = argv[2]; name2 = argv[3];
  } else {
    console.log("usage: scoreboard.ts <file1> <file2>");
    return;
  }
  const parser = len == 3 ? parseLine : parseTest;
  const file1 = `rank/${name1}.txt`;
  const file2 = `rank/${name2}.txt`;
  const data1 = await read(file1, parser);
  const data2 = await read(file2, parser);
  if (data1.length !== data2.length) {
    console.error(`different entry count: ${data1.length} != ${data2.length}`);
    return;
  }
  compare(data1, data2);
}

main().catch(err => console.error(err));
