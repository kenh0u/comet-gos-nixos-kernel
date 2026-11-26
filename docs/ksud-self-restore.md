# ksud 自己復旧 (userdata wipe 対応)

`user` 版 + relocked bootloader の GrapheneOS で、userdata wipe 直後の初回起動から KernelSU を有効にする仕組みです。
公式の導入方法である `init_boot` への boot-patch は AVB と両立しないため、この構成では `ksud` を OS イメージに同梱し、カーネルが注入する init.rc で復元します。

## 問題

方案 C′ (後述) により `kernelsu.ko` は full モードで動き、カーネルは `init.rc` の `on post-fs-data` へ次を注入します。

```
exec u:r:ksu:s0 root -- /data/adb/ksud post-fs-data
```

`/data/adb/ksud` はこれまで `/data` に手で置く物だったため、`flash-all.sh` 等で userdata を wipe すると消えます。
結果:

1. `exec` が ENOENT で失敗し、`ksud` が一度も起動しない
2. throne tracker (Manager アプリの検出) が走らず、Manager は「未インストール」のまま
3. `user` 版は `adb root` ができないので、wipe 後に `ksud` を戻す手段が無い

relocked 端末では fastboot での書き換えもできないため、詰みます。

## 解決

### OS 側: `ksud` を `/system/bin/ksud` として同梱

`vendor/google_devices/comet/ksud/Android.bp` (`patches/device-os/` に収録):

```soong
cc_prebuilt_binary {
    name: "ksud",
    srcs: ["ksud"],
    owner: "google_devices",
    vendor: false,
    strip: { none: true },
    check_elf_files: false,
}
```

`vendor/google_devices/comet/comet.mk`:

```make
PRODUCT_PACKAGES += ksud
```

`ksud` は使用する `kernelsu.ko` と同じバージョンの公式 release から入手してください
(KernelSU の `KernelSU_V<major>_<ksud-version>.apk` 内 `lib/arm64-v8a/libksud.so`、または CI artifact の `ksud-aarch64-linux-android`)。
このリポジトリはバイナリを頒布しないため、各自で用意したファイルを `ksud/ksud` に置いてください。
`system` パーティションへ入るためラベルは `system_file` の既定のままよく、sepolicy の追加は不要です。

### カーネル側: init.rc の注入内容 (`patches/aosp/0015-*.patch`)

```
on post-fs-data
    start logd
    exec u:r:ksu:s0 root -- /system/bin/toybox cp -n /system/bin/ksud /data/adb/ksud
    exec u:r:ksu:s0 root -- /system/bin/toybox chcon u:object_r:ksu_file:s0 /data/adb/ksud
    exec u:r:ksu:s0 root -- /data/adb/ksud post-fs-data
```

## なぜこの形か (実機で 2 回失敗して確定)

| 試み | 結果 | 原因 |
|---|---|---|
| init builtin の `copy /system/bin/ksud /data/adb/ksud` | 復元されない | `u:r:<ctx>` の seclabel は `exec` / `exec_start` にのみ有効。builtin は init 自身のコンテキストで動き、`/data/adb` 配下への書き込みが拒否される |
| `exec ... toybox cp -n ...` のみ | **init が SIGABRT、起動しない** | `cp` が作ったファイルは `adb_data_file` のまま。続く `exec ... /data/adb/ksud` が `avc: denied { execute }` (scontext=`init`, permissive=0) となり、init が abort する |
| `exec ... cp -n` + `exec ... chcon ksu_file` | **成功** | `ksu_file` はカーネルが `domain` へ全許可を注入する file_type |

補足:

- `cp -n` は上書きしないので、既存の `/data/adb/ksud` はそのまま残ります。
- `/data/adb` が無い初回起動では、直前の init 処理 (`on post-fs-data` の `mkdir /data/adb 0700` 相当) で作成済みの前提です。relocked `user` 版でのみ検証しています。
- この rc は full モード (LKM を first-stage init からロード) でのみ注入されます。`late_load: true` では注入されません。

### 起動しないときの切り分け

`lastkmsg` / pstore が取れない relocked `user` 版では、以下で切り分けました。

- Recovery の「View logs」から `lastkmsg` / pstore を見る
- KSU Manager が「未インストール」のまま = `ksud` が起動していない。`/system/bin/ksud` の有無と rc の注入を確認

- `ksud debug info` が `late_load: false` を返すこと

## 署名鍵の罠

kleaf はカーネルをビルドするたびに **モジュール署名鍵を新規に生成** します (`certs/signing_key.*` は bazel-out の `kernel_aarch64/certs/` にのみ存在)。
未署名 `.ko` の再署名前に、次を必ず行ってください。

1. ビルド直後、`certs/signing_key.{pem,x509}` と `scripts/sign-file` を bazel-out の外へ退避する
2. 退避した x509 が dist の `Image` 内に埋め込まれているか確認する
3. 未署名 `.ko` のみをその鍵で署名する (`LD_LIBRARY_PATH` は `prebuilts/kernel-build-tools/linux-x86/lib64`、ASAN 版を避ける)

別ビルドの鍵で署名すると、`CONFIG_LOCK_DOWN_KERNEL_FORCE_CONFIDENTIALITY` 下で insmod が EPERM になり起動しません (vendor モジュールの deferred probe でハング → bootloop)。

AVB の鍵は別物で、module 署名鍵の入れ替わりは relocked OTA に影響しません。

## 検証

- Pixel 9 Pro Fold (`comet`), GrapheneOS `user` 版 `2026112603`, `verifiedbootstate=yellow`, `flash.locked=1`
- `flash-all.sh` による wipe + relock 直後の初回起動で KSU Manager が「動作中」(`32601`, lkm, `late_load: false`)
- 以後、Recovery の `adb sideload` による無 wipe OTA を 3 回連続適用 (`2026112601` → `2602` → `2603`)。いずれも Verified Boot は yellow のまま

## ライセンス

- `ksud` バイナリ: KernelSU 由来。ライセンスは上流 KernelSU に準拠します (本リポジトリの「ライセンス」の項を参照)。
- この文書に含まれる `Android.bp` / `comet.mk` の断片、init.rc: 本リポジトリのライセンスに準じます (GPL-2.0-only)。
