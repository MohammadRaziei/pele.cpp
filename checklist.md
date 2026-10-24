# چک‌لیست pele.cpp

بازنویسی کامل C++ (بدون وابستگی، C++17) از Pele (`github.com/obsidianmd/pele`، نسخه‌ی 1.7.0).
مرجع: خروجی `pele.js` در Node با اندازه‌گیر متن ثابت (جدول Arial). ورودی یکسان ⇒ SVG یکسان.
پیش‌نمایش PNG فقط با `novasvg` (ابزار کمکی، جزو پروژه نیست).

## معماری (شی‌گرا، همه‌چیز در `namespace pele`)
- Strategy: `flat_layout_engine` ← `layered_layouter`؛ `text_measurer` ← `metrics_measurer`؛ `shape` ← شکل‌ها
- Dependency Injection: `compound_layouter` موتور چیدمان را از بیرون می‌گیرد
- Registry: `shape_registry` (انجام شد)؛ `diagram_registry` برای انواع دیاگرام (مرحله ۶)
- Builder: `svg_builder` برای ساخت خروجی SVG
- هر نوع دیاگرام یک کلاس است: `diagram` (رابط) ← `flowchart_diagram` و غیره
- Value objects: `theme`، `render_options`، `render_result`
- خطاها: `pele_error` (از `std::runtime_error`)

## قواعد
- [x] تمام کد C++ است، بدون وابستگی خارجی
- [x] `-ffp-contract=off` تا ترتیب عملیات اعشاری مثل JS بماند
- [x] هر مرحله با داده‌ی تولیدشده از `pele.js` تست می‌شود
- [x] هر مرحله که تمام شد، zip و ارسال (`pele.cpp-step-NN-*.zip`)

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

## مرحله ۲ — چیدمان گروه‌ها (`layout/compound.ts` → `src/compound.cpp`)
- [x] `flat_layout_engine` (Strategy) و `layered_layouter`
- [x] `compound_layouter` با تزریق موتور (Dependency Injection)، کلاس‌های `level` و `session`
- [x] مولد تست `tools/gen_compound_cases.mjs` + `tests/test_compound.cpp`
- [x] ۲٬۸۰۰ گراف تودرتو: تطابق ۱۰۰٪

## مرحله ۳ — زیرساخت متن و SVG
- [x] `ustring` (UTF-16 مثل JS)، `chars.h`، `js_math.h` (`js_round`، `js_hypot` عین V8)
- [x] `text_measurer` (Strategy) و `metrics_measurer` با جدول Arial
- [x] entities (نام‌دار، عددی، placeholderهای Mermaid) — ۶٬۶۰۰ مورد تطابق
- [x] `label_layouter`: markup، markdown، wrap، CJK، آیکون
- [x] `svg::` ابزارها: `num`، `esc`، `esc_text`، `label_svg` — ۲٬۰۰۰ مورد
- [x] `theme`: `font_style`، `resolve_style`، `class_names`، `series_color` — ۲٬۰۰۰ مورد (جدول یونیکد تولیدشده)
- [x] `edge_geometry`: `route_path`، `marker`، `loop_path`، `side_reach` — ۱۲٬۶۰۰ مورد
- [x] `svg_document`، `with_title`، `crossing_map`/`struck_title` — ۱٬۲۰۰ مورد
- [x] `shape_registry` (Registry): ۵۸ شکل، `end_side`، `inset_route` — ۱٬۰۹۶ مورد
- [x] `render_options`

## مرحله ۴ — پیش‌پردازش
- [x] `pele_error` و `error_code`، `locate`، `syntax_error` (پیام‌ها عین Mermaid)
- [x] `diagram_detector`: ۳۷ آشکارساز به ترتیب Mermaid (شامل regex ناقص C4 که فقط اولی لنگر دارد)
- [x] `yaml_value` با ترتیب کلید مثل JS (کلیدهای عددی اول) و `parse_yaml` (block، flow، block scalar، escape)
- [x] `parse_json` (معادل `JSON.parse`)
- [x] `preprocess`: front matter، directive (`%%{init}%%`، `%%{wrap}%%`)، کامنت، تگ HTML
- [x] `encode_entities`، `cleanup_comments`
- [x] تست با مرجع TS: ۲۴٬۰۰۰+ مورد YAML/JSON/detect/locate و ۳۲٬۰۰۰+ مورد preprocess

## مرحله ۵ — فلوچارت
- [x] tokens (۷۷ توکن، تولیدشده از TS) و lexer دستی با همه‌ی regexهای sticky — ۲۰٬۰۰۰+ مورد
- [x] parser (descent بازگشتی، subgraph بدون بازگشت) + `flow_db` (گره‌ها به ترتیب اعلان) — ۲۰٬۰۰۰+ مورد
- [x] `url`: `sanitize_url`، `safe_url`، `link_url`، `image_url`، `linked` — ۱۵٬۰۰۰+ مورد
- [x] `shape_names`، `build_flow_graph` (subgraph جمع‌شده، استایل کلاس‌ها، پیش‌فرض یال‌ها)
- [x] `fit_width` (tighten، turn_to_fit)
- [x] `flowchart_renderer`: خوشه‌ها، ۵۸ شکل، آیکون/تصویر، لینک، tooltip، حلقه‌ی خودی، برچسب یال
- [x] **تطابق بایت‌به‌بایت SVG با `pele.js`**: ۳٬۱۰۰+ فلوچارت تصادفی، همه‌ی پیام‌های خطا هم‌متن
- [ ] نسخه‌های دیگر فلوچارت (swimlane و `FlowVariant`)

## مرحله ۶ — API و ابزار
- [x] `pele.h`: `render()`، `detect_type()`، `supports()`، `render_options`، `render_result`
- [x] `diagram` (رابط) و `diagram_registry` (Registry)؛ `flowchart_diagram`
- [x] CLI: `pele-cli in.mmd > out.svg`
- [x] تست پایان‌به‌پایان `test_e2e` (۱۵۰ دیاگرام ثابت، بدون نیاز به Node)
- [x] `parse()` جدا از `render()` (مدل قابل‌استفاده‌ی مجدد)
- [x] CI (GitHub Actions) — نوشته شد، روی CI واقعی اجرا نشده

## مرحله ۷ — بقیه‌ی انواع دیاگرام (هر کدام یک مرحله)
- [x] زیرساخت: `js_regex` (موتور regex هم‌معنی با JS، تست‌شده روی ۲۸۰ regex واقعی Pele، بیش از ۷۰۰٬۰۰۰ تطابق)
- [x] زیرساخت: `common/tokens.ts` → C++ (`token_def`، `token_set`، `tokenize`، `reader`، `keyword()`، `*_value`) — ۵۳٬۰۰۰+ مورد تطابق روی جدول‌های cynefin، git، packet، architecture، treemap، info (جدول‌های دارای `scanner`/`match` سفارشی با دیاگرام خودشان تست می‌شوند)
- [ ] زیرساخت: بقیه‌ی `common/` (`literal-label`، `outline`، `generics`، `fit`، `fit-text`، `icon`، `accDescr`، `scan`، `caseless`)
- [x] info — ۳٬۳۰۰ دیاگرام تصادفی (SVG و پیام خطا) بایت‌به‌بایت یکسان؛ `tools/gen_info_cases.mjs`، `tests/data/e2e_info`
- [x] زیرساخت: `fit-text` و `literalLabel` — ۲۷٬۰۰۰ مورد (`tools/gen_text_cases.mjs`)
- [ ] pie، sequence، class، state، er، gantt، gitGraph
- [ ] mindmap، timeline، journey، xychart، quadrant، requirement
- [ ] c4، sankey، block، kanban، architecture، radar، treemap
- [ ] سایر انواع (agentflow، usecase، railroad، treeView، venn، cynefin، wardley، swimlane، ishikawa، eventmodeling)

- [x] packet — ۷٬۹۰۰ دیاگرام تصادفی بایت‌به‌بایت (شامل حالت down با `maxWidth`)؛ `tools/gen_packet_cases.mjs`، `tests/data/e2e_packet`

## یادداشت‌ها
- Pele چیدمان dagre/ELK ندارد؛ `layered.ts` خودش نوشته شده و حالا در C++ هم عین آن است.
- `position()` فهرست همسایه‌ها را در جا مرتب می‌کند و مراحل بعد به این ترتیب وابسته‌اند.
- `graph` در C++ باید تنها مرجع `efd`/`etd` باشد (کپی محلی باعث اختلاف می‌شد).

## ابزارهای تست (بیرون از C++)
- `tools/gen_*_cases.mjs`: مرجع TS را روی ورودی تصادفی اجرا می‌کنند (نیاز به `/home/claude/pele` و Node)
- `tools/gen_e2e_cases.mjs` + `tools/compare_e2e.sh`: مقایسه‌ی SVG کامل با `pele.js` روی صدها فلوچارت تصادفی
