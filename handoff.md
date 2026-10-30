# UPDATE (latest session): version 0.5.0, Pele 1.12.0, `er` added (33 ctest)

(Changes are in CHANGELOG.md. Done diagrams: flowchart, info, packet, pie, kanban, mindmap, treemap,
cynefin, class, er. ER lives in one pair of files, `src/er/er.h` and `src/er/er.cpp`; the user
prefers few files. `M_PI` is replaced by `pele::constant::pi`: MSVC is part of CI.)

# UPDATE (latest session): moved to Pele 1.12.0 (version 0.4.0)

(Changes are in CHANGELOG.md. Sections below describe earlier steps.)

# UPDATE: moved to Pele 1.11.0 (version 0.3.0)

(The section below describes the 1.10.0 step; 1.11.0 changes are in CHANGELOG.md. Pele 1.12.0 exists.)

# UPDATE: moved to Pele 1.10.0

State: version 0.2.0, matches Pele 1.10.0, 31/31 ctest, `tools/regen_all.sh --check` shows 0 files
differing. See CHANGELOG.md for what was ported. Everything below this section describes the
1.7.0 state and is otherwise still accurate, except where it says "first task: update to 1.10.0",
which is done.

Next steps, in order:
1. Tag v0.2.0 (nothing was tagged by this session).
2. Pele has newer versions: 1.10.1 and 1.11.0 (~42 commits after 1.10.0, touching errors.ts,
   flowchart, treemap layout/render, tokens). Check out the next tag in /home/claude/pele, rebuild
   dist, run `tools/regen_all.sh`, and fix what ctest shows, as was done for 1.10.0.
3. `class` is done (0.2.0), including `components.ts`. Next: the remaining diagram types, in the
   order of README.md.
4. Math (`$$...$$`) is not ported: Pele only uses it with a math renderer. `with_heights`,
   `line_middle` and `label_line` exist and work without math.

Lessons from this update:
- Generators written for an old version do not produce new syntax: passing tests proved nothing
  until the generators got the new tags and break hints (label, kanban, mindmap, treemap, e2e,
  compound). After a version bump, first check what the TS diff added and make sure a generator
  produces it. Check generated files really contain the characters (a doubled backslash in a JS
  string produced the text `\u00ad`, not a soft hyphen).
- Measure coverage of new code paths with a temporary counter before trusting a green run (the
  compound flat/fallback/refine paths were each hit 40 to 215 times in 600 cases).
- `layered.ts` assignTracks/settle refactors are performance only; not ported, and 2400 compound
  cases still match.

---

# handoff.md — pele.cpp

آخرین به‌روزرسانی: ۲۰۲۶-۱۰-۲۶. نسخه‌ی کتابخانه: **0.1.0** (مطابق Pele **1.7.0**). جدیدترین Pele: **1.10.0**.
این فایل را کامل بخوان؛ بعد `checklist.md` (فهرست کوتاه کارها) و `CHANGELOG.md`.

## ۰. خلاصه در ده خط

- پروژه یک بازنویسی C++17 بدون وابستگی از **Pele** (کتابخانه‌ی TypeScript که Mermaid را رندر می‌کند، ساخته‌ی تیم Obsidian) است.
- **معیار پذیرش:** برای هر متن Mermaid، SVG (یا پیام خطا) **بایت‌به‌بایت** مثل `pele.js` باشد.
- ۸ نوع از ۳۴ نوع دیاگرام کامل است: flowchart، info، packet، pie، kanban، mindmap، treemap، cynefin.
- `class` نیمه‌کاره است: lexer، parser، db، members، generics کامل و تست‌شده‌اند؛ **graph و render مانده**.
- ۲۵ نوع مانده (حدود ۳۶٬۰۰۰ خط TS؛ در اندازه‌گیری ۱.۱۰.۰).
- **کار اول چت بعدی: آپدیت مرجع Pele از 1.7.0 به 1.10.0** (بخش ۱). این بزرگ است و باید قبل از پورت دیاگرام تازه انجام شود.
- روش کار: هر دیاگرام را با **مولد تصادفی** از `pele.js` می‌سازیم و C++ را با آن مقایسه می‌کنیم؛ تا اختلاف صفر نشود تمام نیست (بخش ۴).
- کاربر (MohammadRaziei، ریپوی `github.com/MohammadRaziei/pele.cpp`) فارسی می‌نویسد، CI روی ویندوز و لینوکس دارد، و لاگ‌ها را برای تو paste می‌کند. تو فقط به zip خروجی دسترسی داری، نه به git.
- هر مایل‌استون که تمام شد، **به کاربر بگو نسخه بزند** (بخش ۸).

## ۱. کار اول: آپدیت pele.js به 1.10.0

**چرا اول:** اگر دیاگرام‌های تازه را با TS قدیمی (1.7.0) پورت کنیم، بعداً هر کدام را دوباره باید با TS جدید مقایسه و اصلاح کرد، و داده‌ی تستشان باطل می‌شود. موتور چیدمان (که class/er/state روی آن سوارند) بازنویسی شده است.

### آنچه عوض شده (اندازه‌گیری شده با `git diff 52b5c9c 1.10.0`)

۱۲۷ commit، ۹۱ فایل در `src/`، حدود +۲۴۰۰/−۸۰۰ خط. **نوع دیاگرام تازه‌ای نیامده** (هر دو نسخه ۳۴ پوشه).

| ناحیه | تغییر | روی ما چه اثری دارد |
| --- | --- | --- |
| `layout/layered.ts` | +۴۸۴ −۱۸۹ | چیدمان flowchart؛ `src/layered.cpp`، `test_layered` |
| `layout/compound.ts` | +۵۸۵ −۱۴۴ | چیدمان گروه‌ها؛ `src/compound.cpp`، `test_compound` |
| `layout/components.ts` | **فایل تازه** (۷۲ خط) | باید پورت شود |
| `text/label.ts` | +۲۳۰ −۲۸ | `<sub>`، `<sup>`، `<code>` مونوفضا، `<u>`، `<kbd>`/`samp`/`tt`/`var`/`cite`/`dfn`/`ins`، شکستن روی zero-width space و soft hyphen، حفظ تأکید HTML بین خطوط، «Strip only HTML tags»؛ `src/label.cpp` |
| `text/math.ts` | **فایل تازه** (۵۰ خط) | بخوان ببین کجا استفاده می‌شود |
| `svg/builder.ts` | +۷۲ −۱۶ | از جمله «Filter invalid XML characters»؛ `src/svg.cpp` |
| `errors.ts` | +۴۷ | `src/errors.cpp` |
| `preprocess.ts` | +۳۹ −۱۲ | `src/preprocess.cpp` |
| `svg/{theme,root,shapes,title}.ts`، `text/{entities,measurer}.ts`، `util/{yaml,url}.ts`، `types.ts` | ۳ تا ۲۵ خط هرکدام | هر کدام را با `git diff` ببین |
| دیاگرام‌های پورت‌شده | flowchart ‏۵ فایل (۲۸+/۲۷−)، kanban ‏۴۰+/۱۵−، mindmap ‏۸+/۷−، packet/treemap/cynefin ‏۳ تا ۵ خط، pie و info ‏۱ خط (pie: «Decode labels in invalid-value errors»)، common ‏۲ فایل | کوچک؛ تست‌های e2e پیدایشان می‌کنند |
| `class/*` و `common/generics.ts` | بررسی نشده | چون قبلاً پورت شده‌اند، حتماً `git diff 52b5c9c 1.10.0 -- src/diagrams/class src/diagrams/common` را ببین |

commit های «Layout: …» بیشترشان به‌نظر بهینه‌سازی شمارش تقاطع (crossing) هستند، ولی ممکن است نتیجه‌ی چیدمان را هم عوض کنند. **حدس نزن؛ e2e تعیین می‌کند.**

### مراحل

0. **اول از کاربر بخواه `v0.1.0` را tag بزند** (نقطه‌ی مرجع 1.7.0؛ در بخش ۸). اگر نزده بود، ادامه بده ولی یادآوری کن.
1. در `/home/claude/pele` (مسیر باید همین باشد، بخش ۳):
   ```sh
   git fetch --tags && git checkout 1.10.0     # تگ‌ها بدون v هستند: 1.8.0 1.9.0 1.9.1 1.10.0
   npm ci && npm run build                      # dist/pele.js باید ساخته شود
   ```
2. ثابت‌های نسخه را عوض کن: `include/pele/version.h.in` (`PELE_CPP_REFERENCE_VERSION`)، `src/info/info.cpp` (`kVersion`، دیاگرام info متن «Pele v…» را چاپ می‌کند؛ از `package.json` TS می‌آید)، `CMakeLists.txt` (`PROJECT_VERSION` → 0.2.0)، `README.md`، `CHANGELOG.md`.
3. **همه‌ی داده‌ها را از نو بساز:** `tools/regen_all.sh` (حدود ۲۰ ثانیه). این `tests/data/*` را با TS جدید بازتولید می‌کند، از جمله `regexes.txt` (همه‌ی regex های سورس Pele؛ اگر regex تازه‌ای آمده باشد تست `test_regex` آن را می‌گیرد).
4. بساز و تست کن: `cmake --build build && ctest --test-dir build --output-on-failure`. تست‌های ردشده نقشه‌ی کار را می‌دهند. ترتیب پیشنهادی:
   1. `test_label`، `test_text`، `test_svg`، `test_theme`، `test_shapes`، `test_preprocess`، `test_doc`، `test_front`، `test_url`، `test_regex`، `test_parser`، `test_lexer` (زیرساخت ساده). برای هر کدام `git diff 52b5c9c 1.10.0 -- src/<فایل>` را بخوان و فقط تغییرها را پورت کن، نه کل فایل را از نو.
   2. `text/measurer.ts` عوض شده؛ اگر `metrics_data.inc` باید عوض شود `tools/gen_metrics.py` را اجرا کن. `text/entities.ts` عوض شده؛ جدول `entities_data.inc` را چک کن.
   3. `test_layered`، `test_compound` (بزرگ‌ترین بخش). `components.ts` را پورت کن.
   4. `test_e2e` (flowchart) و بقیه‌ی `test_e2e_*`. اختلاف‌ها را با روش بخش ۴.۳ پیدا کن.
   5. `class`: فقط پورت‌شده‌ها (`test_class_lexer`، `test_class_model`، `test_members`).
5. وقتی هر ۳۱+ تست سبز شد: `tools/regen_all.sh --check` را اجرا کن (باید «۰ اختلاف» بدهد)، zip بساز، و به کاربر بگو **نسخه‌ی 0.2.0** بزند.

اگر بخشی بیش از حد بزرگ بود (مثلاً `layered`)، آن را در چند نوبت انجام بده و وضعیت را در این فایل بنویس. **از Pele 1.7.0 برنگرد** مگر کاربر بخواهد.

## ۲. وضعیت فعلی

### دیاگرام‌های کامل (همه با e2e بایت‌به‌بایت، خطاها هم)

| نوع | کجا | تست |
| --- | --- | --- |
| flowchart | `src/flowchart/` | `test_e2e` (۱۵۰ مورد committed؛ هزاران مورد دیگر ad hoc)، `test_lexer`، `test_parser` |
| info | `src/info/` | `test_e2e_info` (۳۰۰) |
| packet | `src/packet/` | `test_e2e_packet` (۴۰۰؛ شامل حالت down) |
| pie | `src/pie/` | `test_e2e_pie` (۵۰۰)، `test_scanner` |
| kanban | `src/kanban/` | `test_e2e_kanban` (۶۰۰) |
| mindmap | `src/mindmap/` | `test_e2e_mindmap` (۵۰۰) |
| treemap | `src/treemap/` | `test_e2e_treemap` (۵۰۰)، `test_format` |
| cynefin | `src/cynefin/` | `test_e2e_cynefin` (۵۰۰)، `test_seed` |

### class (در حال انجام)

- **انجام شده و تست‌شده:** `src/common/generics`، `src/class/members` (۲۴۰٬۰۰۰ مورد، `test_members`)، `src/class/lexer` (۶۱ توکن، همه تولید می‌شوند، ۱۰۰٬۰۰۰+ مورد، `test_class_lexer`)، `src/class/{types,db,parser}` (۲۴٬۰۰۰+ مدل تصادفی با سریالایز یکسان، `test_class_model`).
- **مانده:** `graph.ts` (۱۷۱ خط)، `markers.ts` (۵۵)، `render.ts` (۶۲۵)، و ثبت در `diagram.cpp`. مدل با **اندیس** (نه اشاره‌گر) ساخته شده؛ `class_model.classes` و `class_index` و … را ببین. رندر از `compound_layout`، `end_side`، `route_path`، `edge_label_svg`، عنوان خوشه، `mark_crossings`، `struck_title`، `with_title`، `linked`، `runs_across`، `tighten` و `turn_to_fit` استفاده می‌کند؛ الگوی همه‌شان در `src/flowchart/render.cpp` است. **بعد از آپدیت Pele (بخش ۱) شروع کن**، چون `class/render.ts` هم عوض شده است.

### زیرساخت مشترک: TS ← C++

| TS | C++ |
| --- | --- |
| `util/chars`، `unicode` | `src/chars.h`، `src/unicode_letter.*` (+ `unicode_data.inc`) |
| `util/yaml` | `src/yaml.cpp` |
| `util/url` | `src/url.*` |
| `text/{label,measurer,entities}` | `src/label.*`، `src/text_measurer.cpp`، `src/entities.*` |
| `svg/{builder,root,theme,shapes,edges,cluster,title}` | `src/svg.*`، `svg_document.*`، `theme.*`، `shapes.*`، `edges.*`، `cluster.*`، `title.*` |
| `layout/layered` | `src/layered.*` |
| `layout/compound` | `src/compound.*` |
| `preprocess`، `detect`، `errors`، `core` | `src/preprocess.*`، `detect.*`، `errors.cpp`، `core.cpp` |
| `diagrams/common/tokens` | `src/common/tokens.*` (`token_def`، `token_set`، `tokenize`، `reader`، `common_tokens::*`) |
| `common/outline` | `src/common/outline.*` (kanban + mindmap) |
| `common/{fit-text,literal-label,icon,generics,fit-width}` | `src/common/{fit_text,literal_label,icon,generics}.*`، `src/fit_width.*` (`turn_to_fit`، `tighten`، `fit_plot`، `cap_width`، `title_room`) |
| JS regex، `toFixed/toPrecision/toExponential`، `parseFloat`، `toLowerCase`، `String(n)` | `src/regex.cpp` (`js_regex`)، `src/js_format.*`، `src/js_parse.h`، `src/lowercase.*`، `src/js_math.h` (`js_number_string`، `js_round`، `js_hypot`، `js_to_fixed0`) |

**هنوز نیست:** `layout/lanes.ts` (۶۲۹ خط، برای swimlane)، `FlowVariant` در رندر flowchart (swimlane لازم دارد)، `breakLabel`/`leftLabelSvg` (هنگام نیاز بیاور)، `text/math.ts` و `layout/components.ts` (جدید در 1.10.0).

### دیاگرام‌های مانده (اندازه‌ی TS در 1.10.0)

| دسته | نوع‌ها (خط) |
| --- | --- |
| **پرکاربردها (اول)** | class ‏۲۳۱۱ (نیمه‌کاره)، er ‏۱۸۰۲، state ‏۲۲۸۲، sequence ‏۲۶۸۷، gantt ‏۲۱۵۹، gitGraph ‏۹۲۲ |
| کوچک | ishikawa ‏۴۸۱، timeline ‏۵۲۰، journey ‏۵۲۱، radar ‏۵۵۳ (`match` سفارشی)، treeview ‏۶۶۰ (`match` سفارشی)، swimlane ‏۷۲ (+lanes ۶۲۹)، eventmodeling ‏۸۰۲، sankey ‏۸۵۹، quadrant ‏۹۶۰، venn ‏۱۰۲۰ |
| متوسط | xychart ‏۱۰۶۵، wardley ‏۱۱۹۸، architecture ‏۱۳۶۹، requirement ‏۱۴۰۳، railroad ‏۱۶۶۵، block ‏۱۹۳۷، c4 ‏۲۰۹۸ |
| بزرگ | usecase ‏۳۰۸۶، agentflow ‏۳۷۶۵ |

حدود ۴۵ تا ۷۰ نوبت کار. class/er/state روی `compound` هستند؛ er و class هر دو `generics` دارند. gantt تاریخ دارد (`dates.ts`)؛ **رفتار `Date` و منطقه‌ی زمانی JS را مثل Number/regex با داده تست کن، حدس نزن.**

## ۳. راه‌اندازی محیط (ماشین تازه)

```sh
# مرجع TS: مسیر باید دقیقاً این باشد (۳۰ ابزار آن را hardcode دارند؛ اگر جای دیگری است symlink بزن)
git clone https://github.com/obsidianmd/pele /home/claude/pele
cd /home/claude/pele && git checkout 1.10.0 && npm ci && npm run build     # دیگر 52b5c9c (=1.7.0) نه
# ابزارها: Node ≥ 20 (ما v22.22.2 داشتیم؛ نسخه‌ی یونیکد Node در سرآیند lowercase_data.inc ثبت است)، g++ ≥ 11 یا clang، cmake + ninja
pip install cmake ninja --break-system-packages
cmake -S pele.cpp -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build
```

- اگر CMake 4 با یک پروژه‌ی قدیمی شکایت کرد: `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`.
- شبکه‌ی محیط معمولاً فقط `github.com`، `registry.npmjs.org`، `pypi.org`، `archive.ubuntu.com` را باز دارد.
- `ctest` فقط از `tests/data/` می‌خواند و **به Node نیاز ندارد**؛ Node فقط برای ساختن داده است.

## ۴. روش کار (حلقه‌ی پورت یک دیاگرام)

### ۴.۱ مراحل

1. **بخوان:** همه‌ی `src/diagrams/<نوع>/*.ts` و هرچه ازش import می‌شود و هنوز پورت نشده. `index.ts` را حتماً (رابط parse/render، ثبت تایپ).
2. **زیرساخت جدید را اول و جدا پورت کن** و مستقیم با TS تست کن (الگو: `gen_format_cases.mjs` + `test_format.cpp`، `gen_members_cases.mjs` + `test_members.cpp`). خطایی که در زیرساخت است در e2e گم می‌شود.
3. **اگر lexer/parser/db جدا از رندر است** (مثل class)، آن‌ها را هم با سریالایز مدل تست کن (`gen_class_model_cases.mjs`)، قبل از رندر.
4. **بنویس** در `src/<نوع>/<نوع>.{h,cpp}` (چند فایل اگر بزرگ است) با الگوی `diagram` و ثبت در `src/diagram.cpp` (کلاس‌های `X_diagram_model` و `X_diagram`، و `r.add(...)`)، و `CMakeLists.txt`. الگو: `src/pie/` یا `src/kanban/`.
5. **مولد e2e:** `tools/gen_<نوع>_cases.mjs [seed] [count] [dir]`: ورودی‌های تصادفی از واژگان دیاگرام (شامل خطا، یونیکد، ایموجی، CRLF، گزینه‌های `%%{init}%%`، front matter، و گزینه‌های رندر `maxWidth/fontSize/padding/icons/idPrefix`). خروجی هر مورد: `N.mmd` + `N.svg` یا `N.err` (+ `N.opts`). از ابزارهای موجود کپی کن.
6. **مقایسه:** `build/tests/test_e2e <dir>` (پوشه یا manifest). اولین اجرا معمولاً اختلاف دارد.
7. **پوشش را اندازه بگیر قبل از اعتماد:** ۱۰۰٪ در اولین اجرا مشکوک است. با یک اسکریپت کوچک بشمار چند SVG تولید شد، چند خطا، و هر مسیر مهم (هر شکل، حالت down، آیکون، لینک…) چند بار اجرا شد. اگر ≥۴۰٪ موارد فقط خطای ساده‌اند، مولد را متعادل کن (چندبار شد: ورودی نامعتبر زیاد، سرآیند حساس به حروف…).
8. **حجم:** ≥۶ seed دیگر × ۲٬۰۰۰ مورد بزن (مثلاً seed های 2 31 777 4242 90210 123456). این‌ها را commit نکن.
9. **مجموعه‌ی رگرسیون:** ۵۰۰ مورد با seed 1 → `node tools/pack_e2e.mjs <dir> tests/data/e2e_<نوع>.txt`، و `add_test` در `tests/CMakeLists.txt`. (`pack_e2e` به‌جای SVG کامل فقط SHA-256 نگه می‌دارد؛ حجم داده‌ی تست را کم نگه دارید.)
10. **تمیز:** بیلد از صفر، `ctest`، و **۰ هشدار** (`grep -cE "warning:|error:"` باید ۰ بدهد).
11. **مستندات:** `handoff.md`، `checklist.md`، `README.md`، و `tools/regen_all.sh` (خط مولد تازه). zip را در `/mnt/user-data/outputs/pele.cpp-step-NN-<نام>.zip` بگذار و با `present_files` نشان بده.

### ۴.۲ تست‌های واحد

مولدهای `gen_*_cases.mjs` TS را با esbuild bundle می‌کنند (`/home/claude/pele/node_modules/esbuild`) و خروجی را به‌صورت متن (رشته‌ها hex16، عددها گاهی bits) می‌نویسند؛ `tests/test_*.cpp` همان را در C++ حساب و مقایسه می‌کند.

### ۴.۳ پیدا کردن اختلاف e2e

```sh
build/pele-cli /tmp/x/N.mmd > /tmp/mine.svg
python3 - <<'P'
a=open('/tmp/x/N.svg',encoding='utf-8').read(); b=open('/tmp/mine.svg',encoding='utf-8').read().rstrip('\n')
i=next((k for k in range(min(len(a),len(b))) if a[k]!=b[k]),None); print(i); print('TS :',a[i-100:i+150]); print('C++:',b[i-100:i+150])
P
```
اشتباه‌های رایج: فاصله‌ی جاافتاده در الگوی رشته (`` ` data-id=…` ``)، `num()` (دو رقم اعشار) به‌جای `js_number_string` یا برعکس (ببین TS کدام را استفاده کرده)، ترتیب ارزیابی عبارت‌های عددی، `||` به‌جای `??`.

## ۵. تله‌های JS ← C++ (همه‌شان یک‌بار هزینه داشته‌اند)

- **رشته‌ها UTF-16 هستند** (`ustring` = `std::u16string`). `charCodeAt` بعد از انتها `NaN` است: در C++ `-1` با تابع کمکی. `Array.from` و `for…of` روی **کدپوینت** می‌چرخند (`code_points`)، `split('')` روی **code unit**.
- **`||`/`if (x)` در برابر `??`:** رشته‌ی خالی، `0` و `NaN` falsy اند. `yaml_value::truthy()` و `to_js_string()` (برای `String(v)`).
- **`trim()`/`\s`** مجموعه‌ی فاصله‌ی JS (شامل `\u00a0`، `\u2028`…): `trim()`/`is_space` موجود. `toLowerCase` کامل یونیکد: `changes_when_lowercased` (فقط «آیا تغییر می‌کند»).
- **اعداد ← متن:** `String(n)` = `js_number_string` (نماد علمی ≥1e21 و <1e-6). `toFixed/toExponential/toPrecision` = `js_format` (**هرگز printf**: روی نیمه‌های دقیق به زوج گرد می‌کند و JS به بالا). `parseFloat` = `js_parse_float` (`1_000` می‌شود `1`). `Number(digits)` = `std::strtod`.
- `Math.round` = `js_round` (نیمه به بالا)، `Math.hypot` = `js_hypot`، `x ** 2` = `x*x`. `sin/cos` در V8 و glibc/MSVC ممکن است در آخرین بیت فرق کنند (روی هزاران دیاگرام ندیده‌ایم؛ اگر CI یک رقم دوم اعشار فرق داشت اینجاست).
- **عملیات ۳۲ بیتی** (`|0`، `>>>`، `Math.imul`) را مثل `src/cynefin/boundaries.cpp` بنویس و با داده‌ی bit-level تست کن.
- **`Map`/`Set`** ترتیب درج دارند و «جایگزینی» جای کلید را نگه می‌دارد: `vector` + `map<key,int>` (اندیس). **اشاره‌گر به عناصر vector ندهید** (با رشد باطل می‌شود)؛ از اندیس استفاده کنید.
- **`Array.sort` پایدار است:** `std::stable_sort` با comparator هم‌ارز (`b - a` ← `a > b`).
- **regex:** فقط `js_regex` (معنای JS: sticky `match_at`، lookahead، lazy، `\s`، `.` بدون `\n\r\u2028\u2029`). هرگز `std::regex`.
- `str.replace('x', …)` فقط اولی؛ `replace(/x/g, …)` همه. `split(/(,)/)` جداکننده را هم برمی‌گرداند.
- **پیکربندی:** `config.x?.[key]` = `settings.find("x")` + `is_object()` + `find(key)` + `is_number()`/`is_string()`/`is_boolean()` (مثل `typeof`). `typeof v === 'number' && v >= min ? Math.min(v,max) : fallback` را کپی کن.
- **خطاها:** `pele_error(message_utf8, error_code, error_info{type})`؛ `to_utf8` برای متن کاربر. پیام‌ها باید حرف به حرف یکی باشند.
- **ترتیب ارزیابی عبارت‌های double مهم است** (`a*b*c` ≠ `a*(b*c)`). کپی کن، «ساده» نکن. `-ffp-contract=off` و `/fp:precise` عمداً هستند (بدون FMA).

## ۶. تله‌های پلتفرم و CI

- **هیچ کاراکتر غیر ASCII در literal های C++** (از `\uXXXX` استفاده کن). MSVC بدون `/utf-8` سورس را با code page می‌خواند (۳۷ مورد `test_label` به همین دلیل روی ویندوز رد می‌شد). `/utf-8` هم اضافه شده. بازتولید روی لینوکس: `-DCMAKE_CXX_FLAGS="-finput-charset=ISO-8859-1 -fexec-charset=UTF-8"`.
- **`long` در ویندوز ۳۲ بیتی است:** برای اندیس‌ها `std::ptrdiff_t`.
- **`-Wshadow` روشن است** (MSVC با `/W4` همان را می‌گیرد). هدف ۰ هشدار.
- `.gitattributes` تبدیل پایان خط `tests/data/**` را خاموش می‌کند، و `test_e2e` یک `\r` انتهای خط manifest را نادیده می‌گیرد.
- `std::to_chars(double, …)` روی libc++ قدیمی (macOS < 13.3) در دسترس نیست؛ `js_number_string` و `js_format` به آن وابسته‌اند.
- `M_PI` قابل‌حمل نیست؛ ثابت خودت را تعریف کن.
- `compile_commands.json` فقط با Ninja/Makefile ساخته می‌شود (target اختیاری `uml` به آن نیاز دارد؛ ساختار مثل libgsp: `-DPELE_ENABLE_UML=ON`، `tools/uml/`).

## ۷. ابزارها و تست‌ها

**`tools/regen_all.sh`:** همه‌ی `tests/data/*` را از TS بازتولید می‌کند. `--check`: بازتولید، مقایسه با داده‌ی committed، و برگرداندن داده‌ی committed. روی نسخه‌ای که داده با آن ساخته شده باید ۰ اختلاف بدهد (فعلاً بدهد). ۱۲ مجموعه‌ی قدیمی (`OLD` در اسکریپت) با آرگومان‌هایی ساخته شده بودند که ثبت نشده؛ `--check` آن‌ها را رد می‌کند و بقیه (از جمله `svg` با seed 4 و count 1600) دقیقاً برابرند. **هنگام اضافه کردن مولد تازه، یک خط به اسکریپت اضافه کن.**

**مولدها (`tools/`):** `gen_<x>_cases.mjs` برای هر تست، `pack_e2e.mjs`، `extract_regexes.mjs`، `gen_lowercase_table.mjs` (→ `src/lowercase_data.inc`)، `gen_unicode.mjs`، `gen_metrics.py`، `gen_shape_names.py`، `svg2png.py` (پیش‌نمایش؛ `novasvg` لازم)، `compare_e2e.sh`.

**تست‌ها (۳۱ تا در `ctest`):** `test_layered`، `test_compound`، `test_label`، `test_svg`، `test_theme`، `test_edges`، `test_doc`، `test_shapes`، `test_front`، `test_preprocess`، `test_lexer`، `test_url`، `test_parser`، `test_e2e`، `test_regex`، `test_tokens` (جدول توکن ۱۳ دیاگرام با داده؛ جدول‌های `scanner` با regex‌شان)، `test_e2e_{info,packet,kanban,mindmap,pie,treemap,cynefin}`، `test_text`، `test_outline`، `test_scanner`، `test_format`، `test_seed`، `test_members`، `test_class_lexer`، `test_class_model`.

**حجم داده‌ی تست:** `tests/data` حدود ۱۵ مگابایت (متن فشرده‌پذیر) است. فایل‌های committed فقط **مجموعه‌ی رگرسیون**اند؛ اجرای سنگین با seed های دیگر بیرون از ریپو (`/tmp`). e2e فقط **SHA-256 SVG** را نگه می‌دارد، نه خود SVG را.

## ۸. نسخه‌ها (کاربر tag می‌زند، تو آماده می‌کنی)

هر وقت یک مایل‌استون تمام شد، **صریحاً بگو «وقت نسخه است»** و آماده کن: `PROJECT_VERSION` در `CMakeLists.txt`، بخش تازه در `CHANGELOG.md`، جدول `README.md`. بعد کاربر:
```sh
git tag -a vX.Y.Z -m "..." && git push --tags
```

| نسخه | محتوا |
| --- | --- |
| **0.1.0** | flowchart + info/packet/pie/kanban/mindmap/treemap/cynefin، مطابق Pele 1.7.0. **آماده؛ کاربر هنوز tag نزده.** |
| 0.2.0 | همان مجموعه، مطابق Pele **1.10.0** (بخش ۱) |
| 0.3.0 | + class، er، state |
| 0.4.0 | + sequence، gantt، gitGraph |
| 1.0.0 | همه‌ی ۳۴ نوع (آرزو) |

## ۹. ساختار فایل‌ها

```
include/pele/     API عمومی (pele.h، errors.h، render_options.h، rendered.h، yaml.h، regex.h، …؛ version.h.in)
src/              زیرساخت مشترک (متن، برچسب، SVG، شکل‌ها، مسیر یال‌ها، چیدمان، regex، yaml، preprocess)
src/common/       tokens، outline، fit_text، literal_label، icon، generics، code_points.h
src/<نوع>/        هر دیاگرام: flowchart info packet pie kanban mindmap treemap cynefin class(ناقص)
src/diagram.cpp   ثبت انواع (کلاس X_diagram_model / X_diagram و r.add)
tests/            test_*.cpp و data/ (مجموعه‌های رگرسیون)
tools/            مولدها، regen_all.sh، pele_cli.cpp، uml/
```
خروجی zip هر مرحله: `/mnt/user-data/outputs/pele.cpp-step-NN-<نام>.zip` (بدون `build/`)، با `present_files`.

## ۱۰. ریسک‌ها و محدودیت‌های شناخته‌شده

- **CI ویندوز را فقط کاربر می‌بیند** (لاگ paste می‌کند). تو فقط لینوکس را اجرا می‌کنی. قبل از تحویل، تله‌های بخش ۶ را چک کن.
- `sin/cos` یک‌بیتی (بخش ۵) و `std::to_chars` (بخش ۶).
- `class/render.ts` و بقیه‌ی دیاگرام‌ها با TS جدید فرق دارند: همیشه از **TS چک‌اوت‌شده‌ی فعلی** بخوان، نه از حافظه.
- مسیر `/home/claude/pele` در ۳۰ ابزار ثابت است (به‌جای متغیر محیطی، چون `import` استاتیک است). اگر خواستی تمیزش کنی، `import()` پویا + `process.env.PELE_TS` لازم است؛ اولویت پایین.

## ۱۱. نکات همکاری با کاربر

- زبان گفتگو **فارسی**، فنی ولی ساده. جواب‌ها صادق باشند: اگر چیزی را اجرا نکرده‌ای نگو «تست شد»؛ اگر ۱۰۰٪ در اولین اجرا آمد، پوشش را بسنج و بگو.
- گاهی کوتاه می‌خواهد («فقط یک جواب کوتاه»)، گاهی درصد پیشرفت («سر انگشتی»). هر مرحله خلاصه: چه ساخته شد، نتایج تست با عدد، محدودیت‌ها، قدم بعدی. اگر چند راه هست، یکی را پیشنهاد کن و دلیلش را بگو.
- کاربر لاگ‌های CI را paste می‌کند (ویندوز: هشدارهای MSVC، شکست تست‌ها). ریشه را پیدا کن و اگر ممکن است روی لینوکس بازتولید کن (مثل مورد `-finput-charset`)، قبل از اصلاح.
- از کاربر خواسته شد: `clang-uml` مثل `mohammadraziei/libgsp` اضافه شود (انجام شد)؛ حجم داده‌ی تست کم باشد (انجام شد)؛ وقت نسخه را بگویم (بخش ۸).
