# Engine sürümleri

Launcher iki engine taşır (engine `Docs/Design/README.md` D16):

- `Engine/`: engine reposunun submodule'ü (`main`). Aktif geliştirme; launcher buna karşı derlenir. Kullanıcıya
  gitmez. Değişiklik iki adımlıdır: önce `Engine/` içinde `main`'de commit + push, sonra burada pointer commit'i.
- `Engines/Engine<ANA.ARA>/`: stable kopyalar. Kurulum paketine giren ve kullanıcı modunda projelere kopyalanan
  bunlardır. Bir seri klasörü o serinin en son yamasını tutar; repoda 2–3 seri durur. Hiç elle düzenlenmez.

## Yeni stable sürüm

1. Engine reposunda: `EngineManifest.json` `version` + `notes` en üste (kurallar engine `Docs/ToolsContract.md` →
   Manifest; engine testi denetler), commit, push, annotated tag `v<sürüm>`, tag'i push.
2. Burada: `git -C Engine fetch --tags` ve `python Scripts/import_engine.py v<sürüm>`.
3. `Engines/` değişikliğini commit'le (ör. "Import engine 0.2.0"). Seri sayısı 3'ü geçerse betik söyler; en eskiyi
   silmek ayrı bir commit'tir.

Yama akışı: eski bir seride düzeltme engine reposunda o tag'den açılan dalda yapılır, yeni yama tag'lenir ve
içe aktarılır; seri klasörü yenisiyle değişir.

## İçe aktarma ne yapar

- Tag'in var olduğunu ve origin'de aynı commit'i gösterdiğini denetler (her kopya izlenebilir).
- İçeriği `git archive <tag>` ile alır: `.git`, commit'lenmemiş değişiklikler ve engine'in `.gitattributes`
  `export-ignore` dosyaları (engine `CLAUDE.md`, `Docs/Design/`) girmez (D14). Neyin ayıklanacağını engine bilir.
- Manifest sürümü tag'le aynı olmalı (`v0.2.0` ↔ `0.2.0`), en yeni not o sürüm olmalı.
- Yanına `EngineCopy.json` yazar: `{"format": 1, "source": {"tag", "commit"}, "files": {"<yol>": "sha256:<hex>"}}`.
  Launcher projelerdeki kopyayı bununla denetler (salt okunur kuralı) ve güncellemede neyin kaybolacağını gösterir.
- Önce yanına yazar, sonra klasörleri değiştirir; seri klasörü sadece daha yeni bir yamaya ilerler.

`Engines/**` git'te `-text`'tir: dosyalar `git archive`'ın ürettiği baytlarla saklanır, özetler tutar.
