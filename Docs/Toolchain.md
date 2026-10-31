# Gömülü araçlar

Launcher Python (resmi embeddable), CMake ve Ninja'yı taşır; kullanıcı kurmaz (D7, D8). MSVC taşınmaz (lisans),
Sistem kontrolü kurar.

- **Kaynak:** `Toolchain.lock.json`: araç başına `version`, `url`, `sha256` (arşivin), `root` (arşivde soyulacak
  klasör), `executable` (araç klasörüne göre). İkililer git'e girmez.
- **İndirme:** `python Scripts/fetch_toolchain.py` → `ToolCache/<araç>/` + `ToolCache/Toolchain.json` (launcher'ın
  okuduğu: sürüm + exe yolu). Özet tutmazsa durur, doğrulanmamış hiçbir şey açılmaz. İnternet sadece burada.
- **Yükseltme:** kilit dosyasında sürüm + adres, `sha256`'yı boşalt, `python Scripts/fetch_toolchain.py --pin`;
  yayıncının yayımladığı özetle karşılaştır (CMake `cmake-<v>-SHA-256.txt` yayımlar), sonra commit.
- **Python:** embeddable dağıtımın `python3XX._pth` dosyası olduğu gibi kalır: yorumlayıcı yalıtılmıştır (sistemdeki
  Python'un registry kaydı, `PYTHONPATH` karışmaz). Engine araçları buna göre yazılır: çalışma klasörüne güvenmez,
  CMake'e kendi yorumlayıcısını verir (engine `Docs/ToolsContract.md`).
- **Çalışırken:** `EngineCommand` aracın klasörlerini alt sürecin `PATH`'inin başına ekler; engine araçları
  cmake / ninja / python'u oradan bulur. `-X utf8` ile çıktı UTF-8'dir.
- **Doğrulama:** Sistem kontrolü her aracı `--version` ile çalıştırır ve sürümü `Toolchain.json` ile karşılaştırır.

AudioGeneration'ın NumPy / SciPy bağımlılıkları gömülü Python'a gelmez (D7); "ses araçlarını kur" Aşama 4'tedir.
