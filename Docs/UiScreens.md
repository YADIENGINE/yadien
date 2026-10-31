# Launcher ekranları (`Apps/Ui`)

`<engine>.exe`'nin penceresi. İş mantığı `LauncherCore`'da ([CoreApi](CoreApi.md)), sıralaması (işler, gezinme,
kayıt, bildirimler) tek uygulama modelinde (`Model/LauncherModel`); ekranlar yalnız çizer. Kararlar engine
`Docs/Design/README.md` → D15 "UI hazırlığı" (Godot proje yöneticisi benzeri düzen, yalnız İngilizce, hata dili).

```
Apps/Ui/
  Main.cpp, LauncherEntryPoint.h   wWinMain; düzeni çözer, pencereyi çalıştırır (raylib ile <windows.h> ayrı)
  LauncherApplication              engine Game'i: ToolUiContext + model; her karede Tick → DrawLauncher
  Model/LauncherModel              bütün durum ve eylemler (aşağıda)
  Dialogs/FolderDialog             Windows klasör seçme penceresi (modelin eylemleri açar)
  Screens/LauncherScreens.h        ekran fonksiyonlarının listesi
  Screens/*.cpp                    bölge başına bir çizim fonksiyonu (görsel iş burada)
```

## Bölgeler

| Fonksiyon | Dosya | Bölge |
| --- | --- | --- |
| `DrawLauncher` | `LauncherFrame.cpp` | Bütün pencere: üst çubuk, bildirim, sekme, durum çubuğu; sonra panel ve onay popup'ı |
| `DrawTopBar` | `LauncherFrame.cpp` | Ad, sekmeler (Projects / Engines), sistem durumu noktası, Settings |
| `DrawNotice` | `LauncherFrame.cpp` | `CurrentNotice()`; `SettingsError()` + "Start with defaults" |
| `DrawStatusBar` | `LauncherFrame.cpp` | Launcher sürümü, proje sayısı, uygulama / iş durumu, "Developer mode" |
| `DrawProjectsTab` | `ProjectsTab.cpp` | Kalıcı araç çubuğu (Create, Import, Projects Folder, filtre, sıralama), sınırlı liste, proje denetçisi |
| `DrawEnginesTab` | `EnginesTab.cpp` | Stable sürümler, notları (kırıcılar öne), kullanan proje sayısı; developer'da "dev (main)" |
| `DrawNewProjectPanel` | `NewProjectPanel.cpp` | Ad, konum (Browse), engine seçimi, canlı doğrulama, Create |
| `DrawSettingsPanel` | `SettingsPanel.cpp` | PATH, kurulum klasörünü aç, developer modu ve lisanslar |
| `DrawSystemCheckPanel` | `SystemCheckPanel.cpp` | Maddeler (durum, ayrıntı), MSVC kurulumu, yeniden denetle |
| `DrawEngineUpdatePanel` | `EngineUpdatePanel.cpp` | Update / Repair / Replace onayı: notlar, kırıcı olabilir, kaybolacak / silinecek dosyalar |
| `DrawJobPanel` | `JobPanel.cpp` | Ön plan işi: başlık, adım (n / toplam), dönen gösterge, canlı çıktı, Cancel; hata + çıktı + Back |
| `DrawConfirmation` | `Confirmation.cpp` | Remove? / developer şifresi / engine commit mesajı |

## Model

Ekran `LauncherModel&` alır: getter'ları okur, eylemleri çağırır. Eylemler kuyruğa girer ve sonraki karenin başında
uygulanır; çizim sırasında (satır döngüsünün içinde bile) çağrılabilir. Formlar (`NewProject()`, `Filter()`,
`ConfirmationInput()`) ekranın doğrudan düzenlediği metinlerdir.

| Bölge | Okunan | Eylemler |
| --- | --- | --- |
| Genel | `ProductName()`, `LauncherVersion()`, `Mode()`, `Tab()`, `Panel()`, `CurrentNotice()`, `SettingsError()`, `System()` | `SelectTab`, `OpenPanel(NewProject / Settings / SystemCheck)`, `ClosePanel`, `DismissNotice`, `StartWithDefaultSettings` |
| Projects | `Rows()`, `VisibleRows()`, `Selected()`, `ActionsFor(row)`, `DetailsFor(row)`, `DetailsRunning()`, `Filter()`, `Sort()`, `ProjectsFolder()` | `Select`, `SetSort`, `Create`, `Import`, `BrowseProjectsFolder`, `SetProjectsFolder`, `Open`, `Run`, `ShowInFolder`, `AskRemove`, `RemoveMissing`, `ChangeEngine(row, Update / Repair / Replace)`, `UpdateEngineFromRemote`, `AskPublishEngine` |
| Engines | `Engines()` (`Engines()`: en yeni üstte, `Problems()`), `ProjectsUsing(version)`, `DevelopmentEngineAvailable()` | — |
| New project | `NewProject()` (`name`, `location`, `engine`), `NewProjectEngines()`, `NewProjectProblem()`, `NewProjectFolder()` | `BrowseNewProjectLocation`, `CreateProject` |
| Engine update | `PendingChange()`, `ChangeProject()`, `PlanRunning()`, `Plan()`, `PlanError()` | `ConfirmEngineChange`, `ClosePanel` |
| System check | `System()`, `Report()`, `SystemCheckRunning()` | `RunSystemCheck`, `InstallMsvc` |
| Settings | `OnPath()`, `DeveloperModeAvailable()`, `Mode()` | `SetOnPath`, `OpenInstallFolder`, `AskDeveloperMode`, `LeaveDeveloperMode` |
| Onay | `Pending()`, `PendingProject()`, `ConfirmationInput()`, `PasswordCheckRunning()`, `PasswordRejected()` | `Confirm`, `CancelConfirmation` |
| İş | `CurrentJob()` (`Title`, `State`, `StepText`, `StepIndex`, `StepCount`, `Error`), `JobFailureTitle()`, `JobOutput()` | `CancelJob`, `DismissJob` |

- Satır metadatası `ProjectRow`'dan: sürüm `Version()`, "Update available" `update` (+ `updateMayBreak`), tür
  `info.kind` (Git: "managed by git", Unregistered: "unknown source"), `contractSupported == false`: "update the
  launcher", `folderMissing`, `info.problem`; ayrıntılar `DetailsFor`: `integrity` temiz değilse "N engine files
  modified" (+ Repair), `git` (dal, değişiklik sayısı, ahead / behind, detached), `error`.
- Ön plan işi sürerken satır eylemleri pasiftir (`ActionsFor` hepsini kapatır, Remove hariç); iş başarıyla bitince
  panel kendiliğinden kapanır (Aç'ta launcher kapanır).
- Bildirim: başlık ekranın değil modelin metnidir; altındaki mesaj çekirdeğin cümlesidir, değiştirilmez.

## Kurallar

- Ekranlar iş mantığı yazmaz: dosya / süreç / git / kayıt yok, çekirdek fonksiyonu çağrılmaz; yalnız model.
  Modelde eksik bir şey varsa ekran onu taklit etmez, modele eklenir.
- Görünüş engine'in ortak araç arayüzüdür: `ToolUi/ToolTheme.h` (renkler `ToolToneColor`, fontlar `ToolFont`,
  ölçek `ToolUiScale()`), `ToolUi/ToolWidgets.h`; ikonlar Font Awesome (`<extras/IconsFontAwesome6.h>`, `ICON_FA_*`).
  Tema / widget değişikliği engine'de olur (editörle ortak); launcher'a özgü küçük yardımcılar `Screens/`'ta.
- Metinler İngilizce, ekran fonksiyonlarında; ürün adı yazılmaz (`ProductName()`).
- Ölçüler `ToolUiScale()` ile çarpılır; sabit sayılar dosyanın anonim namespace'inde adlı `constexpr`.
- Araç çubuğu ve sütun başlıklı liste boşken de görünür. Boşluk mesajı listenin içindedir;
  sonuçsuz aramada Clear search bulunur. Satır içeriği 48 mantıksal px, ad / konum ve engine / durum sütunlarıdır.
  Projects ve Project details başlıkları ayrı `PanelHeader` şeritleridir; tablo başlıkları yarı kalındır.
- Import Project'in sağındaki Projects Folder klasör seçiciyi açar. Seçilen klasör doğrulanır, varsayılan proje
  klasörü olarak kaydedilir, yeni proje formunun konumu güncellenir ve otomatik taranır. Mevcut kayıtlı projeler
  korunur. Düğmenin ipucu geçerli yolu gösterir. İlerleme / iptal iş panelindedir, sonuçlar bildirimde gösterilir.
- Sağ denetçi 316 mantıksal px'dir; görünür seçimde projenin bilgilerini / eylemlerini gösterir.
  Seçim yokken geçerli projeler klasörü, aynı seçim / tarama eylemi, Create / Import kısayolları ve kısa keşif
  açıklaması görünür; ayrı Launcher sekmesi ya da ölçek ayarı bulunmaz.
  Dar görünümde Project details açılır panelinden erişilir. Launcher sekmesi ve ayrı tarama satırı yoktur.
  Eksik proje temizleme yalnız eksik proje varsa gösterilir.
- Pencere monitörün DPI ölçeğini izler; kullanıcıya özel arayüz ölçeği ayarı saklanmaz. Settings içinde yalnız
  kurulum / PATH, developer modu ve lisanslar bulunur; varsayılan klasör Projects Folder üzerinden değiştirilir.
