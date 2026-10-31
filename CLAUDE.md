# Launcher (C++20, Windows)

Engine'in launcher'ı: ayrı ürün, ayrı repo. Mimari `Docs/Architecture.md`, çekirdek API'si `Docs/CoreApi.md`,
pencere ve ekranları `Docs/UiScreens.md`. Tasarım kararları engine reposunda (`Engine/Docs/Design/README.md`,
D3–D19); karar değişirse orada güncellenir.

**Ad sabit yazılmaz.** Ürün adı engine manifest'indedir (`Engine/EngineManifest.json` `name`); kod
`LAUNCHER_PRODUCT_NAME` / `LAUNCHER_EXECUTABLE_NAME` kullanır, belgeler `<engine>` yazar.
**Hiçbir oyunu bilmez.** Oyun adı, yolu ya da kuralı yazılmaz; örnekler `MyGame`.
**Derleme mantığı yazmaz.** Derleme / test / editör projenin `Engine/Tools`'undadır; launcher onu kendi Python'uyla
çalıştırır ve argümanları aynen devreder. Engine'in iç dosyalarını (`Tools/` modülleri, `Internals/`) okumaz;
sözleşme engine `Docs/ToolsContract.md` (komutlar, çıkış kodları, manifest).

## Engine
- `Engine/` submodule'dür (engine reposu, `main`): aktif geliştirme, launcher buna karşı derlenir. Değişiklik iki
  adımlı: `git -C Engine switch main`, orada commit + push, sonra burada pointer commit'i. Commit / push öncesi
  `git -C Engine status` ve `git -C Engine pull --rebase`; başka bir session aynı engine'de çalışıyor olabilir.
- Bir oyunun içinde (`<oyun>/Launcher/`) launcher oyunun `Engine/`'ine karşı derlenir, kendi `Engine/` submodule'ü
  başlatılmaz; pointer'ı `python Scripts/dev.py pin-engine` ile çekilir. Sıra: engine → launcher → oyun
  (`Docs/Development.md` → Bir oyunun içinde geliştirmek).
- `Engines/Engine<ANA.ARA>/` stable kopyalardır: sadece `python Scripts/import_engine.py v<sürüm>` yazar, elle
  düzenlenmez (`Docs/Engines.md`).

## Build ve test
Build almak kullanıcıya bırakılır (kod bitince kullanıcı derler, uyarı / hata paylaşır).
`python Scripts/dev.py build | test | clean` (`Docs/Development.md`). Betik testleri derleyicisiz:
`python -m unittest discover -s Scripts/Tests -t .`. İlk kurulumda `python Scripts/fetch_toolchain.py`.

## Kurallar
- Bütün iş `Core/` (`LauncherCore`, headless); `Apps/` giriş noktaları incedir. UI iş mantığı yazmaz: durum ve
  eylemler `Apps/Ui/Model/LauncherModel`'de, `Apps/Ui/Screens/` yalnız çizer.
- Çekirdek raylib / ImGui include etmez. Hata kanalı `Result<T>` / `Status`; exception API'den çıkmaz. Metin UTF-8
  `std::string`, yol `std::filesystem::path`. Uzun işler `OutputLine` + iptal bayrağı alır; thread'i yalnız `Jobs/`
  açar (`Job<T>`).
- Pencerenin görünüşü engine'in `ToolUi/`'ıdır (editörle ortak); launcher kendi temasını / widget sistemini yazmaz.
- Kullanıcı modu git çalıştırmaz; git işleri sadece `Developer/DeveloperGit`'te.
- Kod stili engine'inki: C++20, `/W4` uyarısız, isimli namespace yok (yardımcılar anonim namespace'te, açılış /
  kapanışta boş satır yok), her class kendi `.h` + `.cpp`'sinde, `#pragma once`, include'lar tek blok (kendi header →
  üçüncü parti → std → proje), `new` / `delete` ve C dizisi yok, RAII, `enum class` tek satır, `PascalCase` /
  `camelCase` / `_camelCase`, getter'lar `[[nodiscard]] const`. Kod metinleri, yorumlar ve yeni belgeler İngilizce
  (D19); Türkçe belgeler çevrilene kadar yerinde düzenlenir.
- Repo public, lisans MIT (`LICENSE`). Oyun adı, kişisel bilgi, şifre / anahtar yazılmaz.
- Format `.clang-format`, satır sonu LF; commit öncesi değişen dosyalar:
  `& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\clang-format.exe" -i <dosyalar>`.
- Yeni `.cpp` → `Core/CMakeLists.txt` (`LauncherCore`), `Apps/Ui/CMakeLists.txt` ya da `Tests/CMakeLists.txt`. Yeni
  davranışın testi `Tests/`'e; `[git]` testleri yerel depolarla.
- Commit mesajları kısa, emir kipinde, İngilizce.
