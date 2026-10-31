# Kullanıcı modu ve developer modu

Mod launcher'ın yetkisidir; projenin türünü dosyalar söyler (engine `Docs/Design/README.md` D4).

| Tür (`ProjectInfo::kind`) | Nasıl anlaşılır | Salt okunur denetimi | Engine güncellemesi |
| --- | --- | --- | --- |
| `Copy` | `Engine/EngineCopy.json` var | var | `Engines/`'dan, onaylı (her iki modda) |
| `Git` | `Engine/.git` var (submodule) | yok | kullanıcı modunda yok; developer modunda git işleri |
| `Unregistered` | ikisi de yok | yok, uyarı | "`Engines/`'daki bir sürümle değiştir", onaylı |
| `Missing` | kullanılabilir `Engine/` yok | — | `Project.json` varsa aynı "değiştir" |

Derleme / çalıştırma / editör her türde çalışır. Kullanıcı modu hiçbir zaman git çalıştırmaz (D14).

## Developer modu

- Ayarlar'da düz bir anahtardır; açarken bir uyarı onayı çıkar ("`main`'deki kararsız engine; engine'e katkı için").
  Şifre yoktur (engine D19): engine reposu public, göndermeyi GitHub'ın push yetkisi korur.
- Developer modunda: yeni projede "dev (main)" seçeneği (submodule), `Git` projelerde "Engine'i güncelle" ve "Engine
  değişikliklerini gönder", Sistem kontrolünde git maddesi.
