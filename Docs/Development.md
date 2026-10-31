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

Testler: C++ `Tests/` (Catch2, `[git]` etiketli olanlar yerel git depolarıyla çalışır, git yoksa atlanır); betikler
`python -m unittest discover -s Scripts/Tests -t .`.
