# Çekirdek API'si (UI session'ı için devir belgesi)

Launcher'ın bütün işi `LauncherCore`'dadır (headless, `Core/`); UI sadece onu çağırır, kendi iş mantığı yazmaz.
Tasarım kararları engine reposunda `Docs/Design/README.md` (D4, D6, D8, D14, D15, D16); bu belge onların koddaki
karşılığıdır. Mimari: [Architecture](Architecture.md).

## Durum (2026-10-31)

Bitti (bu session): repo iskeleti, engine submodule'ü + ilk stable kopya (`Engines/Engine0.1`, `v0.1.0`), içe aktarma
betiği, araç kilidi ve indirme betiği, developer şifresi betiği, çekirdek kütüphane, `<engine>.com` CLI, testler.

UI session'ının işi:
- `Apps/Ui/` (`<engine>.exe`, WIN32): ekranlar Projeler, Yeni proje, Sistem kontrolü, Ayarlar (D15). Engine'in
  editör ImGui katmanı ve teması kullanılır; yeni UI sistemi yazılmaz.
- `LAUNCHER_BUILD_UI=ON` engine'in raylib runtime'ını açar (kök `CMakeLists.txt`); editörün ImGui hedefleri
  (`EngineEditorImGui`, `EngineEditorUi`) şu an `ENGINE_BUILD_EDITOR` altında ve kısmen `Internals/`'ta. Launcher'ın
  kullanacağı ImGui katmanı engine'de public hale getirilir (engine'de iki adımlı akış; launcher submodule'e karşı
  derlendiği için tag beklemez). Launcher engine `Internals/` include etmez.
- Uzun işler (sistem kontrolü, MSVC kurulumu, yeni proje, engine güncellemesi, editörü derleme, git işleri) UI
  thread'inde çalışmaz: bir worker thread'de çalıştırılır, `OutputLine` satırları UI'a kuyrukla taşınır, iptal
  `std::atomic<bool>` ile.

## UI'dan önce yapılacaklar (UI hazırlığı)

Ekranlar yalnızca görsel iş olarak (düzen, buton yeri, görünüş) yazılabilsin diye, UI'dan önce bir session'da
yapılır. Her biri karar ister; tek tek konuşulur ve engine `Docs/Design/README.md`'ye yazılır.

1. **Paylaşılan ImGui katmanı (engine):** tema (`Editor/Ui/Style/EditorTheme`), widget'lar (`Editor/Ui/Widgets`),
   pencere + ImGui bağlamı + font kurulumu (`Editor/Host/EditorUiContext`) bugün `ENGINE_BUILD_EDITOR` altında,
   `EngineEditorUi` / editör exe'sine bağlı. Editörden bağımsız, launcher'ın link'leyebileceği bir katmana ayrılır;
   editör de onu kullanır (kopya yok).
2. **İş modeli (çekirdek):** arka plan işi tipi: thread, `OutputLine` kuyruğu, ilerleme / durum (sürüyor, bitti,
   hata, iptal), iptal. UI her karede sadece durumu okur. Testli.
3. **"Aç" akışı (çekirdek):** salt okunur denetimi → `editor --build-only` → `StartDetached(editor)` →
   `MarkProjectOpened` + kaydet; adımları ve hataları olan tek bir iş.
4. **"Proje listesine dön" (engine + launcher):** editör launcher'ı bilmez; launcher editörü kendi yoluyla başlatır
   (ör. ortam değişkeni / argüman), editör menüsü onu açar. Mekanizma karar ister.
5. **Eksik çekirdek parçaları:** PATH'e ekle / çıkar (HKCU, `WM_SETTINGCHANGE`), "Kurulum klasörünü aç", klasör seçme
   penceresi (Windows dialog; UI tarafı platform yardımcısı), developer modunda commit seçimi (şu an sadece dal),
   proje satırının hazır modeli (sürüm, tür, güncelleme var, kırıcı olabilir, git durumu, sorun).
6. **UI hedefi iskeleti:** `Apps/Ui` (`<engine>.exe`, WIN32), pencere + tema + ekranlar arası gezinme + boş ekran
   fonksiyonları; ekran başına bir çizim fonksiyonu, uygulama durumu tek bir model sınıfında. Görsel tasarım bu
   iskeletin ekran fonksiyonları doldurularak yapılır.
7. **Açık kararlar:** UI dili (metinler İngilizce mi, yerelleştirme var mı), ekran düzeni ve gezinme, hata / uyarı
   dili, ilerleme gösterimi.

## Genel kurallar

- Hata kanalı `Result<T>` / `Status` (`Platform/Result.h`): `Ok()`, `Value()`, `Error()`. Mesajlar İngilizce, tek
  cümle, sonda nokta yok; UI olduğu gibi gösterebilir. Exception public API'den çıkmaz.
- Metin UTF-8 `std::string`; yollar `std::filesystem::path` (`PathToUtf8` / `Utf8ToPath`, `Platform/Text.h`).
- Süreç çıktısı `OutputLine` (`std::function<void(const std::string&)>`), satır satır, UTF-8. Callback çağıran
  thread'de çağrılır (UI worker thread'i); UI thread'ine kendisi taşır.
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
| `LauncherSettings { mode, projects, projectsFolder, uiScale }` | `Settings.json` (`format: 1`) |
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

## Proje satırı (`Projects/ProjectInfo.h`)

```cpp
ProjectInfo info = InspectProject(entry.root); // hızlı, dosya okur; her satır için çağrılabilir
```

| Alan | Anlamı |
| --- | --- |
| `name` | `Project.json` `name` (yoksa klasör adı) |
| `kind` | `EngineKind::Copy` (kullanıcı kopyası), `Git` (submodule, developer projesi), `Unregistered` (kaydı yok), `Missing` (engine yok / bozuk) |
| `manifest` | `EngineManifest { name, version, toolsContract, repository, notes }`; `Missing`'de boş |
| `problem` | boş değilse satırda uyarı (ör. klasör silinmiş) |

Satır rozetleri (D15):
- **Sürüm:** `info.manifest->version.ToString()`.
- **Güncelleme var:** `kind == Copy` ve `engines.UpdateFor(info.manifest->version) != nullptr`.
- **Kırıcı olabilir:** `MayBreak(from, to)` (0.x'te ARA, 1.0'dan sonra ANA adımı).
- `Git`: "engine git ile yönetiliyor" notu; kullanıcı modunda engine düğmeleri gizli. `Unregistered`: "kaynağı
  bilinmiyor" uyarısı + "Engines/'daki bir sürümle değiştir". `toolsContract` desteklenmiyorsa
  (`SupportsToolsContract`, `Cli/EngineCommand.h`) "launcher'ı güncelle".

## Aç

1. Kopyada salt okunur denetimi (D4): `LoadEngineCopyRecord(info.EngineFolder())` + `CheckCopyIntegrity(...)`
   → `CopyIntegrity { modified, missing, added }`; `Clean()` değilse uyarı + "Onar" (aynı sürümle
   `PlanEngineUpdate`). ~0,2–1 s: worker thread.
2. Editörü derle (ilerleme): `LauncherEngineRunner(toolchain, &cancel)(root, {"editor", "--build-only"}, onLine)`.
   `toolchain = LoadToolchain(layout.toolsFolder)` (`System/Toolchain.h`).
3. Başlat: `StartDetached(EngineCommand(toolchain, info, {"editor"}).Value())` (`Platform/Process.h`); derleme
   güncel olduğu için hemen açılır. `MarkProjectOpened` + kaydet, launcher kapanır.

Launcher derleme mantığı yazmaz; her şey projenin `Engine/Tools`'unda (D7). Editör açıkken derleme engine'in
`refresh` komutuyla editöre gider.

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

- Kurulum, PATH'e ekleme, kısayol, Uygulamalar kaydı, üzerine kurulan güncelleme: Aşama 4.
- `launcher new` CLI işi (AI / betik için proje kurma): istenirse `CreateProject` üstüne ince bir iş.
- AudioGeneration bağımlılıklarını kurma (D7, "ses araçlarını kur"): gömülü Python'a pip gerekir; Aşama 4'te.
