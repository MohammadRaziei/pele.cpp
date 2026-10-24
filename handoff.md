# handoff — pele.cpp

این فایل برای کسی است که کار را از اینجا ادامه می‌دهد (خودت در چت بعدی یا شخص دیگر).
حالت پروژه: **مراحل ۱ تا ۶ کامل‌اند؛ زیرساخت regex، `common/tokens`، `fit_text`، `literal_label` کامل است؛ `info` و `packet` بایت‌به‌بایت با TS یکسان‌اند.**

## هدف

`pele.cpp` بازنویسی کامل C++17 و بدون وابستگی از [Pele](https://github.com/obsidianmd/pele)
(نسخه‌ی 1.7.0، مجوز MIT، تیم Obsidian) است. **معیار درستی: برای متن یکسان، SVG بایت‌به‌بایت یکسان با `pele.js`**
(در Node، با اندازه‌گیر متن ثابت = جدول Arial، یعنی `metricsMeasurer`).
`pele.cpp` کاملاً جدا از `mermaid.cpp` است (آن را نخوانده‌ایم و لازم نیست).

## وضعیت

| بخش | وضعیت |
| --- | --- |
| چیدمان لایه‌ای (`layered`) و گروه‌ها (`compound`) | کامل، تطابق ۱۰۰٪ با TS روی ده‌ها هزار گراف تصادفی |
| متن/برچسب/entities، ابزارهای SVG، تم، یال‌ها، ۵۸ شکل، cluster/title/root | کامل و تست‌شده |
| پیش‌پردازش (front matter، directive، کامنت)، YAML، JSON، detect، خطاها | کامل و تست‌شده |
| **فلوچارت** (lexer، parser، db، graph، render، url) | کامل؛ ۳٬۱۰۰+ فلوچارت تصادفی بایت‌به‌بایت یکسان |
| API عمومی (`render`، `parse`، `detect_type`، `supports`)، CLI، `diagram_registry` | کامل |
| `js_regex` (موتور regex شبیه JS) | کامل؛ هر ۲۸۰ regex واقعی Pele، بیش از ۷۰۰٬۰۰۰ تطابق |
| `common/tokens.ts` → `src/common/tokens.h/.cpp` (`token_def`، `token_set`، `tokenize`، `reader`، `common_tokens::*`) | کامل؛ ۵۳٬۰۰۰+ مورد تطابق روی جدول‌های ۶ دیاگرام |
| **info** | کامل؛ ۳٬۳۰۰ دیاگرام تصادفی بایت‌به‌بایت (SVG و پیام خطا) یکسان |
| **packet** (parser، مدل با خطاهای semantic، رندر across و down) | کامل؛ ۷٬۹۰۰ دیاگرام تصادفی بایت‌به‌بایت یکسان (۳۳۷ مورد در حالت down) |
| `common/fit-text`، `common/literal-label` (`literalLabel`) | کامل؛ ۲۷٬۰۰۰ مورد. `breakLabel` و `leftLabelSvg` هنوز پورت نشده‌اند (هر کدام را هنگام نیاز بیاور) |
| بقیه‌ی `common/` (`outline`، `generics`، `fit`، `icon`، `scan`، `caseless`، …) | **شروع نشده** |
| بقیه‌ی انواع دیاگرام (۳۰ نوع) | شروع نشده |

آخرین اجرای `ctest`: ۱۹ تست، همه پاس.

## نکته‌ی مهم درباره‌ی مرجع TS

`git clone` ریپوی Pele آخرین نسخه (**1.9.0** هنگام نوشتن) را می‌دهد، ولی پروژه روی **1.7.0** است. پس از کلون حتماً:
`cd /home/claude/pele && git checkout 52b5c9c && npm ci && npm run build` (وگرنه `dist/pele.js` با داده‌ی C++ نمی‌خواند).

## تغییر در کد مشترک (مراحل قبل)

`js_number_string` (`src/js_math.h`) حالا عین `String(n)` است: نماد علمی برای ≥1e21 و <1e-6 (قبلاً همیشه fixed بود). با Node روی ۳٬۷۰۰ عدد مقایسه شد.
`tests/test_e2e.cpp` فایل اختیاری `N.opts` (`maxWidth=`، `fontSize=`، `padding=`) را می‌خواند، برای دیاگرام‌هایی که به عرض میزبان واکنش نشان می‌دهند.

## نکات ویندوز / MSVC (CI روی windows-latest)

- **هیچ کاراکتر غیر ASCII در literal های C++ ننویس**؛ از `\uXXXX` استفاده کن (مثل `kClose`/`kOpen` در `src/label.cpp`). MSVC بدون `/utf-8` سورس را با code page سیستم می‌خواند و literal خراب می‌شود (۳۷ مورد `test_label` به همین دلیل روی ویندوز رد می‌شد). `/utf-8` هم در `CMakeLists.txt` برای MSVC اضافه شده. بازتولید روی لینوکس: `-DCMAKE_CXX_FLAGS="-finput-charset=ISO-8859-1 -fexec-charset=UTF-8"`.
- `.gitattributes` تبدیل پایان خط را برای `tests/data/**` خاموش می‌کند و `test_e2e` یک `\r` انتهای خط manifest را هم نادیده می‌گیرد (داده‌ی واقعی `\r` را escape کرده است).

## ساخت و تست

```sh
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
./build/pele-cli diagram.mmd > out.svg
```

`ctest` فقط از فایل‌های `tests/data/` می‌خواند و به Node نیاز ندارد.
در حالت عادی `test_e2e` یک **manifest** (`tests/data/e2e*.txt`) می‌خواند: یک خط برای هر دیاگرام با متن، گزینه‌ها و **SHA-256 خروجی `pele.js`** (یا کد و پیام خطا). خود SVGها ذخیره نمی‌شوند (قبلاً ۱۲ مگابایت بود، حالا ~۱۸۰ کیلوبایت). اگر آرگومان یک پوشه باشد (`N.mmd` + `N.svg`/`N.err` + `N.opts`، خروجی `gen_*_cases.mjs`) همان را مستقیم و با SVG کامل مقایسه می‌کند، که برای دیدن اینکه دقیقاً کجا فرق دارند لازم است.

## محیط مرجع (برای تولید داده‌ی تست)

مولدهای `tools/gen_*.mjs` به این مسیرها وابسته‌اند (مسیرها hard-coded هستند؛ روی ماشین جدید عوضشان کن):

1. `git clone https://github.com/obsidianmd/pele /home/claude/pele` (من commit `52b5c9c` را داشتم)، بعد `npm ci && npm run build` تا `dist/pele.js` ساخته شود.
2. مولدها با `esbuild` از `/home/claude/pele/node_modules` بسته‌ها را می‌سازند و تابع‌های TS را مستقیم اجرا می‌کنند.
3. `tools/extract_regexes.mjs` ← `/tmp/regexes.json` ← `tools/gen_regex_cases.mjs` (به `esprima` داخل node_modules Pele نیاز دارد).
4. پیش‌نمایش PNG: `pip install novasvg` و `python3 tools/svg2png.py in.svg out.png [width]`. novasvg از `var(--x)` پشتیبانی نمی‌کند، پس این اسکریپت اول متغیرها را با مقدار پیش‌فرض‌شان جایگزین می‌کند (فقط برای دیدن؛ خروجی SVG را عوض نمی‌کند).

## روش کار برای هر بخش (همان که تا اینجا جواب داده)

1. سورس TS را بخوان (حتماً کامل؛ جزئیات رفتاری مهم‌اند).
2. به C++ ببر، **با همان ترتیب عملیات**.
3. یک مولد `tools/gen_<x>_cases.mjs` بنویس که تابع TS را روی ورودی تصادفی اجرا کند و نتیجه را به متن بریزد (رشته‌ها hex)، و `tests/test_<x>.cpp` که همان را از C++ بسنجد.
4. چند seed امتحان کن؛ پوشش را بشمار (چند مورد خطا، چند مورد موفق، کدام شاخه‌ها).
5. وقتی کل دیاگرام آماده شد: `tools/gen_e2e_cases.mjs` را الگو بگیر، رندر کامل با `pele.js` بساز، `tools/compare_e2e.sh` را اجرا کن، و حدود ۱۵۰ مورد را به `tests/data/e2e/` اضافه کن (نام فایل‌ها را با پیشوند نوع دیاگرام بساز تا با فلوچارت قاطی نشود).
   برای نگه‌داشتن یک مجموعه‌ی تازه: `node tools/gen_X_cases.mjs <seed> <n> /tmp/X` سپس `node tools/pack_e2e.mjs /tmp/X tests/data/e2e_X.txt` و یک `add_test` به `tests/CMakeLists.txt`. پوشه‌ی خام را commit نکن.

## معماری (شی‌گرا، همه‌چیز در `namespace pele`)

- `include/pele/`: API عمومی (`pele.h`، `render_options.h`، `errors.h`، `text_measurer.h`، `icons.h`، `yaml.h`، `regex.h`، `ustring.h`، `rendered.h`).
- **Strategy:** `flat_layout_engine` ← `layered_layouter`؛ `text_measurer` ← `metrics_measurer`؛ `shape` ← ۵۸ شکل.
- **Dependency Injection:** `compound_layouter` موتور چیدمان را از بیرون می‌گیرد.
- **Registry:** `shape_registry`؛ `diagram_registry` (در `src/diagram.cpp`). هر نوع دیاگرام یک کلاس `diagram` است با `parse()` و `render()`. برای نوع جدید: کلاس را بنویس و در `diagram_registry::builtin()` اضافه کن.
- `src/flowchart/` نمونه‌ی کامل یک نوع دیاگرام است (tokens، lexer، db، parser، graph، render).

## تصمیم‌ها و دام‌ها (مهم؛ اشتباه‌های گذشته)

- **متن UTF-16 است** (`pele::ustring = std::u16string`). اندازه‌گیری، wrap و lexer در JS روی واحدهای UTF-16 کار می‌کنند و برای تطابق باید همان باشد. فقط در مرز API و خروجی SVG به UTF-8 تبدیل می‌شود.
- **`-ffp-contract=off`** در CMake الزامی است (جلوگیری از FMA). `Math.hypot` در V8 الگوریتم خودش را دارد و در `js_math.h` عیناً پیاده شده (`js_hypot`)؛ از `std::hypot` استفاده نکن. `Math.round` = `floor(x+0.5)` (`js_round`).
- **ترتیب کلیدهای شیء JS:** کلیدهای عددی اول و به ترتیب عددی، بعد بقیه به ترتیب درج. `yaml_value::set` این را رعایت می‌کند.
- **Pele dagre/ELK ندارد**؛ `layout/layered.ts` را خودش نوشته و عیناً پورت شده است. `position()` فهرست همسایه‌ها را **در جا** مرتب می‌کند و مراحل بعدی به آن ترتیب وابسته‌اند.
- هر جا TS آرایه‌ی مشترک را تغییر می‌دهد، در C++ باید همان یک شیء باشد (یک بار با `graph.efd` اشتباه کپی گرفتم).
- **regexهای JS** را دستی ترجمه نکن؛ از `js_regex` استفاده کن (sticky با `match_at`، جست‌وجو با `search`). رفتار backtracking، گروه‌های داخل حلقه (هر تکرار ریست می‌شود) و قانون «تکرار تهی بعد از min رد می‌شود» پیاده شده و با تست ثابت شده است.
- `regex` قبلاً فقط در لکسرهای دستی (فلوچارت) به‌صورت دستی ترجمه شده بود؛ نسخه‌های دیگر باید از `js_regex` استفاده کنند.
- `sh` (نه bash) brace expansion ندارد: `mkdir -p {a,b}` یک پوشه‌ی بی‌معنی می‌سازد. (در zip مراحل ۱ تا ۴ یک پوشه‌ی اضافی به نام `{include` هست؛ بی‌ضرر است و حذفش کن.)
- فایل‌های مرجع SVG بدون newline پایانی‌اند؛ `pele-cli` یک `\n` اضافه می‌کند، و `tools/compare_e2e.sh` با `$(cat)` آن را نادیده می‌گیرد. `test_e2e` مقایسه‌ی دقیق می‌کند.
- متن `info` خروجی «Pele v1.7.0» دارد؛ برای تطابق با مرجع همین رشته را (به‌صورت ثابت) نگه دار.

## محدودیت‌های شناخته‌شده

- نرمال‌سازی آدرس `http:`/`https:` (فقط وقتی آدرس `//` ندارد) تقریبی است؛ IDN/punycode و %-decode میزبان را ندارد (`src/url.cpp`، `normalize_http`).
- `toLowerCase` در اعتبارسنجی نام شکل (`flow_db::add_vertex`) فقط ASCII را در نظر می‌گیرد.
- `canonical()` در `regex.cpp` (حالت `i`) فقط ASCII، Latin-1، یونانی و سیریلیک پایه را پوشش می‌دهد.
- نسخه‌های دیگر فلوچارت (`swimlane`، `FlowVariant`) را پورت نکرده‌ام.
- مجدداً: مولدهای تست به `/home/claude/pele` و Node نیاز دارند.

## قدم بعدی (به ترتیب)

1. **انجام شد — `common/tokens`** (`src/common/tokens.h/.cpp`). طراحی: `token_def` (سازنده‌ی روان: `token_def::regex(name, source, first)`، `.hidden()`، `.opener()`، `.longer()`، `.scanner()`، `.match()`) یک `token_ptr` (`shared_ptr<const token_type>`) می‌دهد؛ `token_set` جدول را یک‌بار آماده می‌کند (مثل `prepare` در TS)؛ `longer` با **هویت شیء** حل می‌شود (مثل `types.indexOf`)، پس نوع مشترک باید یک‌بار ساخته شود (`common_tokens::newline()` و غیره static هستند). `reader` معادل `Reader`. `scanner` و `match` به‌صورت `std::function` هستند و هنوز با داده تست نشده‌اند (فقط ۵ دیاگرام از آن‌ها استفاده می‌کنند: wardley، railroad، pie، treeview، radar؛ هنگام پورت آن‌ها تستشان کن).
2. **انجام شد — `info`** (`src/info/info.h/.cpp`، ثبت‌شده در `diagram.cpp`). نسخه‌ی چاپ‌شده «Pele v1.7.0» ثابت است (`kVersion`). الگوی کار برای دیاگرام بعدی: همین فایل‌ها را کپی کن، یک `class xxx_diagram final : public diagram` در `diagram.cpp` اضافه کن، و `tests/data/e2e_<type>` را با مولد مشابه `tools/gen_info_cases.mjs` بساز.
3. بقیه‌ی `common/` به‌اندازه‌ی نیاز هر دیاگرام (`literal-label`، `outline`، `generics`، `fit`، `fit-text`، `icon`، `accDescr`، `scan`، `caseless`).
4. به ترتیب اندازه، تقریبی از سورس TS (**تذکر:** `pie`، `treeview`، `radar` و `wardley` از `scanner`/`match` سفارشی استفاده می‌کنند؛ `gen_tokens_cases.mjs` جدولشان را رد می‌کند): `pie` (~۳۶۰ خط)، `packet` (~۳۶۰)، `kanban` (~۳۸۰)، `mindmap` (~۴۰۰)، سپس بقیه‌ی کوچک‌ها، و در آخر بزرگ‌ترین‌ها مثل `sequence` (~۲٬۷۰۰ خط)، `class`، `state`، `er`، `gantt`. (اندازه‌ها را قبل از شروع دوباره بشمار: `wc -l /home/claude/pele/src/diagrams/<type>/*.ts`.)
5. موارد باز: `swimlane`/`FlowVariant`، اجرای CI روی GitHub، بازبینی README.

## نقشه‌ی فایل‌ها

```
include/pele/   API عمومی
src/            مشترک: ustring/chars/js_math، text_measurer، label، entities، svg، theme، edges، shapes،
                cluster، title، svg_document، layered، compound، errors، detect، yaml، preprocess، url،
                regex، fit_width، diagram، core
src/common/     tokens (tokenizer مشترک دیاگرام‌های Langium)، fit_text، literal_label، code_points
src/info/       info (parser، مدل، رندر)
src/packet/     packet (parser، مدل، رندر)
src/flowchart/  tokens، lexer، db، parser، graph، render، shape_names
tests/          test_*.cpp و tests/data/ (داده‌ی مرجع تولیدشده از TS)
tools/          gen_*.mjs، extract_regexes.mjs، compare_e2e.sh، svg2png.py، pele_cli.cpp
checklist.md    چک‌لیست مراحل
```
