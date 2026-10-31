# Launcher'ı geliştirmek

Gerekenler: VS 2022 Build Tools (C++ workload), CMake 3.24+, Python 3.10+, git. Klon:
`git clone --recurse-submodules` (sonradan `git submodule update --init`).

```powershell
python Scripts/fetch_toolchain.py     # ToolCache/: launcher'ın taşıdığı Python, CMake, Ninja (bir kez)
python Scripts/dev.py build           # Debug: build/Debug/<engine>.com
python Scripts/dev.py build --release
python Scripts/dev.py test            # Debug derle + C++ testleri (ctest) + betik testleri
python Scripts/dev.py clean
```

`dev.py` MSVC ortamını engine submodule'ünün yardımcısından alır. Elle: Developer PowerShell'de
`cmake --preset launcher`, `cmake --build --preset debug`, `ctest --preset debug`.

Geliştirme build'i repo'nun `Engines/` ve `ToolCache/` klasörlerini kullanır, ayarlarını `build/<Config>/LauncherData/`'da
tutar (`LauncherLayout.json`). Deneme: `build\Debug\<engine>.com launcher system-check`, bir proje klasöründe
`...\<engine>.com build`.

## Bir oyunun içinde geliştirmek

Launcher reposu bir oyunun reposuna `Launcher/` submodule'ü olarak da konabilir; engine, launcher ve oyun aynı
yerde geliştirilir, engine'in tek kopyası oyunun `Engine/`'idir:

```
MyGame/
  Engine/      oyunun engine submodule'ü (tek engine kopyası)
  Launcher/    bu repo; içindeki Engine/ submodule'ü başlatılmaz (git submodule update --init Launcher, recursive değil)
```

- `Scripts/` komutları launcher'ın bir projenin içinde durduğunu görünce (üst klasörde `Project.json` + `Engine/`)
  o `Engine/`'i kullanır (`Scripts/engine_location.py`); `--engine PATH` her zaman önce gelir. CMake'e
  `LAUNCHER_ENGINE_DIR` olarak geçer.
- Launcher'ın kendi Engine pointer'ı elle tutulur: engine değişikliği push'landıktan sonra
  `python Launcher/Scripts/dev.py pin-engine` (pointer'ı engine'in HEAD'ine çeker; push'lanmamış commit'i reddeder),
  sonra launcher reposunda commit + push, sonra oyunun reposunda `Launcher` pointer'ı.
- Sıra her zaman: engine → launcher → oyun. Her repo sadece push'lanmış commit'leri gösterir.

Testler: C++ `Tests/` (Catch2, `[git]` etiketli olanlar yerel git depolarıyla çalışır, git yoksa atlanır); betikler
`python -m unittest discover -s Scripts/Tests -t .`.
