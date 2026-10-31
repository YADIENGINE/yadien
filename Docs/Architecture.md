# Launcher mimarisi

Launcher engine'den ayrı bir üründür (engine `Docs/Design/README.md` D15, D16): engine'in stable sürümlerini taşır,
projeleri kurar, açar ve engine komutlarını projenin kendi engine'ine devreder. Hiçbir oyunu bilmez; engine'in adını
sabit yazmaz (ad engine manifest'inden gelir).

## Repo

```
Engine/              submodule: engine reposu, main (aktif geliştirme). Launcher buna karşı derlenir.
Engines/             stable kopyalar, Engine<ANA.ARA>/ (Scripts/import_engine.py; hiç düzenlenmez) — Engines.md
Core/                LauncherCore: headless çekirdek, include kökü (#include "Projects/NewProject.h")
  Platform/          Result/Status, UTF-8 ↔ UTF-16, dosya (atomik yazım, ağaç kopyası), süreç, BCrypt, bilinen klasörler
  Engines/           EngineVersion, EngineManifest (notlar), EngineCopyRecord, EngineLibrary (Engines/ taraması)
  Projects/          ProjectInfo (tür), CopyIntegrity (salt okunur), EngineUpdate (plan + uygulama), NewProject
  Settings/          LauncherLayout, LauncherSettings (Settings.json), ProjectList
  Developer/         DeveloperAccess (şifre), DeveloperGit (submodule işleri)
  System/            Toolchain (Toolchain.json), SystemCheck (araçlar, vswhere, winget, git)
  Cli/               EngineCommand (devir), CommandLine (<engine>.com)
Apps/
  Cli/Main.cpp       <engine>.com (konsol)
  Ui/                <engine>.exe (pencere) — UI session'ı ekler (LAUNCHER_BUILD_UI)
Tests/               Catch2 testleri (LauncherCoreTests) + Support/
Scripts/             Geliştirici betikleri (stdlib Python): import_engine, fetch_toolchain, set_developer_password, dev
Toolchain.lock.json  Gömülü araçların sürüm / adres / SHA-256'sı — Toolchain.md
DeveloperAccess.json Developer şifresinin PBKDF2 özeti (yoksa mod açılamaz) — Modes.md
ToolCache/           İndirilen araçlar (git'e girmez)
```

## Katmanlar

- Bütün iş `LauncherCore`'dadır; giriş noktaları incedir. UI ve CLI aynı fonksiyonları çağırır (D6: tek mantık).
- Çekirdek raylib / ImGui bilmez; sadece engine'in `ThirdParty/nlohmann_json`'unu ve Windows API'lerini (BCrypt,
  Shell) kullanır. UI hedefi engine'in ImGui katmanını ayrıca link'ler.
- Launcher derleme mantığı yazmaz: derleme, test, editör projenin `Engine/Tools`'undadır (D7). Launcher onu kendi
  Python'uyla çalıştırır (`EngineCommand`).
- Uzun işler `OutputLine` ile çıktı akıtır ve iptal bayrağı alır; thread yönetimi çağıranındır.

## CLI

`<engine>.com` konsol programıdır, `<engine>.exe` pencere programı; terminalde `<engine>` yazınca Windows `.com`'u
önce seçer (D15).

```
<engine> [--project KÖK] <engine komutu> [seçenekler]   → <python> -X utf8 <KÖK>/Engine --project <KÖK> <...>
<engine> launcher version | system-check | projects [--json]
```

- Proje: `--project` yoksa çalışma klasöründen yukarı ilk `Project.json`'lu (ya da `Engine/EngineManifest.json`'lu,
  `new` öncesi) klasör.
- Manifest'in `toolsContract`'ı `SupportsToolsContract`'ta değilse komut çalışmaz (çıkış 2, "launcher'ı güncelle").
- Kopya projede her komuttan önce salt okunur denetimi; değişmiş dosya varsa stderr'e uyarı (derlemeyi durdurmaz).
- Süreç konsolu paylaşır; Ctrl+C çocuğa gider, çıkış kodu aynen döner (0 / 1 / 2 / 130).
- Engine `launcher` adında komut eklemez (engine `Docs/ToolsContract.md`).

## Çalışma anındaki düzen

Kurulu (Aşama 4): `%LOCALAPPDATA%\<Ad>\` altında `<engine>.exe`, `<engine>.com`, `Engines/`, `Tools/` (Toolchain.json +
python, cmake, ninja), `Settings.json`. Geliştirme build'inde exe yanındaki `LauncherLayout.json` repo'nun `Engines/`
ve `ToolCache/` klasörlerini, `build/<Config>/LauncherData/` veri klasörünü gösterir; kurulu launcher'ın ayarlarına
dokunulmaz.

## Kod kuralları

Engine'inkilerle aynı: C++20, `/W4` uyarısız, isimli namespace yok (`.cpp` yardımcıları anonim namespace'te), her
class kendi `.h` + `.cpp`'sinde, `#pragma once`, include'lar tek blok (kendi header → üçüncü parti → std → proje),
`new` / `delete` ve C dizisi yok, RAII, `enum class` tek satır, `PascalCase` / `camelCase` / `_camelCase`, getter'lar
`[[nodiscard]] const`. Format `.clang-format`, satır sonu LF. Koddaki metinler ve yorumlar İngilizce; belgeler Türkçe.
