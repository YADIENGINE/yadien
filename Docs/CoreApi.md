# Çekirdek API'si

Launcher'ın bütün işi `LauncherCore`'dadır (headless, `Core/`); UI sadece onu çağırır, kendi iş mantığı yazmaz.
Tasarım kararları engine reposunda `Docs/Design/README.md` (D4, D6, D8, D14, D15, D16); bu belge onların koddaki
karşılığıdır. Mimari: [Architecture](Architecture.md).

## Durum (2026-10-31)

- Çekirdek (repo, içe aktarma, araç kilidi, CLI, ayarlar, proje işlemleri, modlar) ve UI hazırlığı bitti: arka plan
  işi (`Jobs/`), "Aç" / "Run" akışları, proje satırı modeli ve ayrıntıları, Scan, PATH, Explorer, klasör seçme
  penceresi, `Apps/Ui` iskeleti (pencere, tema, model, boş ekran fonksiyonları).
- Kalan iş yalnız görsel: `Apps/Ui/Screens/` fonksiyonlarını doldurmak ([UiScreens](UiScreens.md)). Kararlar engine
  `Docs/Design/README.md` → D15 "UI hazırlığı".
- Pencere engine'in ortak araç arayüzünü kullanır: `EngineToolUi` (`ToolUi/`, engine `Docs/ToolUi.md`).

## Genel kurallar

- Hata kanalı `Result<T>` / `Status` (`Platform/Result.h`): `Ok()`, `Value()`, `Error()`. Mesajlar İngilizce, tek
  cümle, sonda nokta yok; UI olduğu gibi gösterebilir. Exception public API'den çıkmaz.
- Metin UTF-8 `std::string`; yollar `std::filesystem::path` (`PathToUtf8` / `Utf8ToPath`, `Platform/Text.h`).
- Süreç çıktısı `OutputLine` (`std::function<void(const std::string&)>`), satır satır, UTF-8. Callback çağıran
  thread'de çağrılır; işlerde `JobContext::Lines()` verilir, satırlar `BackgroundJob`'ta birikir.
- Uzun işler UI thread'inde çalışmaz: `Job<T>` (aşağıda) kendi thread'ini açar; çekirdek fonksiyonları thread açmaz.
- Ad: ürün adı `LAUNCHER_PRODUCT_NAME`, exe adı `LAUNCHER_EXECUTABLE_NAME`, launcher sürümü `LAUNCHER_VERSION`
  (CMake tanımları; ad engine manifest'inden). UI metinlerine ad sabit yazılmaz.

## Açılış

```cpp
Result<LauncherLayout> layout = ResolveLauncherLayout(ExecutablePath().parent_path()); // Settings/LauncherLayout.h
Result<LauncherSettings> settings = LoadLauncherSettings(layout.Value().SettingsFile());
EngineLibrary engines = EngineLibrary::Scan(layout.Value().enginesFolder);           // Engines/EngineLibrary.h
```

- `LauncherLayout`: `enginesFolder`, `toolsFolder`, `dataFolder`, `SettingsFile()`. Kurulu launcher'da hepsi exe'nin
  yanında; geliştirme build'inde exe yanındaki `LauncherLayout.json` (CMake yazar) repo'nun `Engines/`,
  `ToolCache/` klasörlerini ve build içinde bir veri klasörünü gösterir.
- Bozuk `Settings.json` hatadır (sessizce sıfırlanmaz): UI kullanıcıya söyler, "varsayılanla başla" seçeneği verir.
- İlk açılışta (D15) Sistem kontrolü otomatik açılır: `Settings.json` yokken `LoadLauncherSettings` varsayılan döner,
  UI bunu `std::filesystem::exists(SettingsFile())` ile anlar.

## Ayarlar ve proje listesi (`Settings/`)

| API | İş |
| --- | --- |
| `LauncherSettings { mode, projects, projectsFolder, uiScale, projectSort }` | `Settings.json` (`format: 1`) |
| `SaveLauncherSettings(file, settings)` | atomik yazım; her değişiklikten sonra UI çağırır |
| `AddProject(settings, root)` | "Var olanı ekle": klasörde `Project.json` olmalı, listede olmamalı |
| `RemoveProject(settings, root)` | "Listeden kaldır": diske dokunmaz |
| `MarkProjectOpened(settings, root, CurrentTimeIso())` | "Aç"ta; listede yoksa ekler |
| `RecentProjects(settings)` | ekran sırası: son açılan üstte, hiç açılmamışlar sonda |
| `ProjectsFolderOf(settings)` / `DefaultProjectsFolder()` | yeni projenin önerilen konumu (`Belgeler\<Ad> Projects`) |

`LauncherMode::User` / `Developer`. Developer modunu açmak:

```cpp
std::optional<DeveloperAccessRecord> access = EmbeddedDeveloperAccess(); // Developer/DeveloperAccess.h
// nullopt: bu launcher şifresiz derlendi, mod açılamaz (Ayarlar'da anahtar pasif + açıklama)
if (access && CheckDeveloperPassword(*access, typed)) settings.mode = LauncherMode::Developer;
```

`CheckDeveloperPassword` PBKDF2 (600 000 tur) hesaplar, ~0,3 s: worker thread'de. Kapatmak şifre istemez.

## Arka plan işi (`Jobs/`)

```cpp
auto job = std::make_unique<Job<SystemReport>>("System check", 1, [layout, mode](JobContext& context) -> Result<SystemReport> {
    context.Step("Checking tools");                 // adım metni + sırası (1'den)
    return RunSystemCheck(layout, mode);            // context.Lines(): çıktı, context.CancelFlag(): iptal
});
// UI thread'i her karede: job->State() (Running / Succeeded / Failed / Canceled), StepText(), StepIndex(),
// StepCount(), LinesFrom(next) (yeni satırlar; son BackgroundJob::MaxLines tutulur), Error(), Cancel()
// Bittiğinde: *job->Outcome() → Result<T> (Job<void>'da Status)
```

- `BackgroundJob` tipsiz tabandır (UI yalnız onu okur); `Job<T>` sonucu ekler. Yıkılan iş iptal edilir ve thread'i
  beklenir. Gövde hata döndürürken iptal istenmişse durum `Canceled`'dır; gövdenin attığı exception `Failed`'dır.
- Yüzde yok: adım + "donmadı" göstergesi + canlı çıktı (D15).

## Proje satırı (`Projects/ProjectRow.h`, `ProjectDetails.h`)

```cpp
std::vector<ProjectRow> rows = InspectProjectRows(settings, engines); // hızlı, sadece dosya okur
std::vector<std::size_t> visible = VisibleRows(rows, filter, settings.projectSort);
ProjectActions actions = ActionsFor(rows[i], engines, settings.mode);  // sağ eylem panelinin düğmeleri
std::vector<ProjectDetails> slow = InspectProjectDetails(rows, mode, cancel); // arka plan işinde
```

| `ProjectRow` | Anlamı |
| --- | --- |
| `entry` | `root`, `lastOpened` |
| `info` | `ProjectInfo`: `name`, `kind` (`Copy` / `Git` / `Unregistered` / `Missing`), `manifest`, `problem` |
| `Version()` | "0.1.2"; engine yoksa boş |
| `folderMissing` | klasör silinmiş ("Remove missing") |
| `update`, `updateMayBreak` | kopyada daha yeni stable sürüm ve kırıcı olabilir mi (`MayBreak`) |
| `contractSupported` | `false`: launcher bu engine'i süremez, "launcher'ı güncelle" |

- `ProjectActions { open, run, showInFolder, updateEngine, repairEngine, replaceEngine, engineGit, remove }`;
  hedef sürüm `UpdateTarget` / `RepairTarget` / `ReplaceTarget`.
- `ProjectDetails { root, integrity, git, error }`: kopyanın salt okunur denetimi (`CopyIntegrity`: değişmiş /
  eksik / eklenmiş dosyalar) ve developer modunda git engine'in durumu (`EngineGitStatus`).
- `ProjectsUsing(rows, version)`: Engines sekmesinde "N proje kullanıyor".
- `Git`: "engine git ile yönetiliyor" notu; kullanıcı modunda engine düğmeleri pasif. `Unregistered`: "kaynağı
  bilinmiyor" uyarısı + Replace engine.

## Aç ve Run (`Projects/ProjectLaunch.h`)

```cpp
Status OpenProject(info, toolchain, LauncherEngineRunner(toolchain, context.CancelFlag()), StartDetached, context);
Status RunProject(info, LauncherEngineRunner(toolchain, context.CancelFlag()), context);
```

- Aç, `OpenProjectSteps` (3) adım: "Checking engine files" (kopyada salt okunur denetimi; değişiklik bir `warning:`
  satırıdır, açılış sürer) → "Building editor" (`editor --build-only`) → "Starting editor" (`editor` ayrık başlar).
  Başarıda çağıran `MarkProjectOpened` + kaydeder, launcher kapanır.
- Run: engine'in `run`'ı (derler, oyunu açar, oyun kapanınca biter); iptal derlemeyi durdurur ya da oyunu kapatır.
- Engine komutlarına `ENGINE_LAUNCHER` (`WindowedLauncherExecutable()`) konur: editörün "Back to project list"i
  (engine `Docs/ToolsContract.md` → Ortam değişkenleri).

Launcher derleme mantığı yazmaz; her şey projenin `Engine/Tools`'unda (D7).

## Scan, klasörler, PATH

| API | İş |
| --- | --- |
| `FindProjects(folder, cancel)` (`Projects/ProjectScan.h`) | "Scan": `Project.json`'lu klasörler, en çok `MaxScanDepth` (4) seviye; projenin içine, gizli ve build klasörlerine girmez |
| `OpenFolder(path)` (`Platform/Shell.h`) | "Show in folder", "Open install folder" |
| `LauncherFolder()`, `IsOnUserPath` / `AddToUserPath` / `RemoveFromUserPath` (`System/UserPath.h`) | Ayarlar → PATH (HKCU, `WM_SETTINGCHANGE`) |
| `PickFolder(title, initial)` (`Apps/Ui/Dialogs/FolderDialog.h`) | Windows klasör seçme penceresi (UI tarafı) |

## Yeni proje (`Projects/NewProject.h`)

```cpp
NewProjectRequest request{name, location, *engines.FindSeries("0.1")};           // kullanıcı modu: stable kopya
NewProjectRequest dev{name, location, DevelopmentEngine{engines.Newest()->manifest.repository, "main"}}; // developer
Status usable = CheckNewProjectFolder(request);                 // canlı doğrulama: ad klasör olabilir mi, klasör boş mu
Result<std::filesystem::path> root = CreateProject(request, LauncherEngineRunner(toolchain, &cancel), onLine);
if (root.Ok()) { (void)AddProject(settings, root.Value()); SaveLauncherSettings(...); }
```

- Ad kuralı engine'indir (`new --name`, harf/rakam/`_`); hata çıktıda görünür. Konum + ad = proje klasörü.
- Kopya: `Engines/<seri>` → `<klasör>/Engine`, sonra engine'in `new`'i. Git'e dokunmaz (D14).
- Developer ("dev (main)", sadece developer modunda listelenir): `git init` + submodule (`repository`, dal) +
  `new` + ilk commit "Create <ad>".
- Hata olursa oluşturulan her şey silinir.

## Engine güncellemesi (`Projects/EngineUpdate.h`)

```cpp
Result<EngineUpdatePlan> plan = PlanEngineUpdate(info, *engines.UpdateFor(info.manifest->version)); // worker thread
// Onay ekranı: plan.notes (aradaki bütün sürümler, en yeni üstte; breaking[i].change / .migrate öne çıkar),
// plan.mayBreak, plan.overwritten (kaybolacak düzenlemeler), plan.deleted (silinecek kullanıcı dosyaları)
Status applied = ApplyEngineUpdate(plan.Value());                                                    // onaydan sonra
```

- Her zaman onaylı (D15). Eski sürüme dönülmez; aynı sürüm "onar"dır. `Git` türünde reddedilir.
- Uygulama yanına kopyalayıp klasörleri değiştirir; hata olursa eski engine yerinde kalır. Proje editörü / oyunu
  açıkken Windows klasörü taşıyamaz; hata mesajı bunu söyler.

## Developer projesi git işleri (`Developer/DeveloperGit.h`)

Sadece `LauncherMode::Developer` ve `kind == Git` iken gösterilir.

| API | Düğme |
| --- | --- |
| `ReadEngineGitStatus(root)` → `EngineGitStatus { branch, trackedBranch, head, changedFiles, ahead, behind, pointerChanged, Detached() }` | satır durumu (değişiklik sayısı, detached uyarısı) |
| `UpdateEngineFromRemote(root, onLine)` | "Engine'i güncelle": engine temiz olmalı; detached düzelir, fast-forward, projede "Update Engine" commit'i |
| `PublishEngineChanges(root, message, onLine)` | "Engine değişikliklerini gönder": engine'de commit + pull --rebase + push, projede pointer commit'i (proje push'lanmaz) |

## Sistem kontrolü (`System/SystemCheck.h`)

```cpp
SystemReport report = RunSystemCheck(layout, settings.mode); // ~1 s, worker thread
for (const CheckItem& item : report.items) // id, title, status (Ok / Warning / Missing), detail, fix
if (item.fix == CheckFix::InstallMsvc) InstallMsvc(onLine, &cancel); // winget; uzun; sonra RunSystemCheck yeniden
report.ReadyToBuild()                                                 // Missing yoksa true
```

Maddeler: `python`, `cmake`, `ninja` (kilit dosyasındaki sürüm), `msvc` (vswhere), `engines`, developer modunda `git`
(kurulu mu, `user.name` / `user.email`).

## CLI (`Cli/CommandLine.h`)

`<engine>.com` `RunCommandLine`'ı çağırır; UI'ın işi yok. `<engine> launcher version | system-check | projects
[--json]`; geri kalan her şey projenin engine'ine aynen gider (D6). Ayrıntı: [Architecture](Architecture.md#cli).

## Bu session'da bilerek yapılmayanlar

- Kurulum, kısayol, Uygulamalar kaydı, üzerine kurulan güncelleme: Aşama 4 (PATH'e ekleme Ayarlar'da var).
- Developer projesinde dal / tag / commit seçimi: plan dışı; "dev (main)" dalın en son commit'inden başlar.
- `launcher new` CLI işi (AI / betik için proje kurma): istenirse `CreateProject` üstüne ince bir iş.
- AudioGeneration bağımlılıklarını kurma (D7, "ses araçlarını kur"): gömülü Python'a pip gerekir; Aşama 4'te.
