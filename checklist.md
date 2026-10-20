# چک‌لیست pele.cpp

بازنویسی کامل C++ (بدون وابستگی، C++17) از Pele (`github.com/obsidianmd/pele`، نسخه‌ی 1.7.0).
مرجع: خروجی `pele.js` در Node با اندازه‌گیر متن ثابت (جدول Arial). ورودی یکسان ⇒ SVG یکسان.
پیش‌نمایش PNG فقط با `novasvg` (ابزار کمکی، جزو پروژه نیست).

## قواعد
- [x] تمام کد C++ است، بدون وابستگی خارجی
- [x] `-ffp-contract=off` تا ترتیب عملیات اعشاری مثل JS بماند
- [x] هر مرحله با داده‌ی تولیدشده از `pele.js` تست می‌شود
- [ ] هر مرحله که تمام شد، zip و ارسال

## مرحله ۰ — آماده‌سازی
- [x] خواندن ریپوی Pele و ساختار `src/`
- [x] ساخت `pele.js` و گرفتن خروجی مرجع
- [x] ابزار پیش‌نمایش PNG (`svg2png.py`)
- [x] اسکلت پروژه: CMake، `.gitignore`، git

## مرحله ۱ — چیدمان لایه‌ای (`layout/layered.ts` → `src/layered.cpp`)
- [x] orient، assign_ranks
- [x] reduce_crossings، position (Brandes-Köpf)
- [x] spread، align_labels، settle، snap
- [x] assign_tracks، straighten، past_labels
- [x] مولد تست `tools/gen_layered_cases.mjs` + `tests/test_layered.cpp`
- [x] ۳٬۶۰۰ گراف تصادفی: تطابق ۱۰۰٪

## مرحله ۲ — چیدمان گروه‌ها (`layout/compound.ts`)
- [x] `compound.h`
- [ ] `compound.cpp`
- [ ] مولد تست و تطابق با TS

## مرحله ۳ — زیرساخت متن و SVG
- [ ] جدول متریک Arial (`text/metrics.ts`) و `metrics_measurer`
- [ ] entities، label (markup، wrap، CJK)
- [ ] svg builder: `num`، `esc`، `label_svg`
- [ ] theme، root (`svg_document`)، ROOT_STYLE
- [ ] edges: `route_path`، marker، `edge_label_svg`، `loop_path`
- [ ] shapes (حدود ۵۰ شکل)
- [ ] cluster، title

## مرحله ۴ — پیش‌پردازش
- [ ] preprocess: front matter، directive، comment
- [ ] YAML ساده‌شده (`util/yaml.ts`)
- [ ] detect نوع دیاگرام
- [ ] errors (`pele_error`)

## مرحله ۵ — فلوچارت
- [ ] tokens، lexer
- [ ] parser
- [ ] db (vertex، edge، subgraph، class، style، click)
- [ ] graph (`build_flow_graph`)
- [ ] shapes map
- [ ] render
- [ ] تطابق SVG با `tests/compat` و رندرهای مرجع

## مرحله ۶ — API و ابزار
- [ ] `pele.h`: `render()`، `parse()`، `render_options`
- [ ] CLI: `pele-cli in.mmd > out.svg`
- [ ] تست تطابق خودکار SVG (diff با `pele.js`)
- [ ] CI

## مرحله ۷ — بقیه‌ی انواع دیاگرام (هر کدام یک مرحله)
- [ ] pie، sequence، class، state، er، gantt، gitGraph
- [ ] mindmap، timeline، journey، xychart، quadrant، requirement
- [ ] c4، sankey، block، packet، kanban، architecture، radar، treemap
- [ ] سایر انواع (agentflow، usecase، railroad، treeView، venn، cynefin، wardley، swimlane، ishikawa، eventmodeling، info)

## یادداشت‌ها
- Pele چیدمان dagre/ELK ندارد؛ `layered.ts` خودش نوشته شده و حالا در C++ هم عین آن است.
- `position()` فهرست همسایه‌ها را در جا مرتب می‌کند و مراحل بعد به این ترتیب وابسته‌اند.
- `graph` در C++ باید تنها مرجع `efd`/`etd` باشد (کپی محلی باعث اختلاف می‌شد).
