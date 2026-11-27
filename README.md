# comet-gos-nixos-kernel — GrapheneOS (Pixel 9 Pro Fold / comet) 向け KSU + NixOS コンテナ拡張カーネル差分

GrapheneOS 公式カーネルツリー（branch `17`, `6.1.177-android14-11`）に対して、KernelSU (LKM) と NixOS systemd コンテナ（`barec`）稼働に必要なカーネル構成・パッチをまとめた差分リポジトリです。

対象デバイス: Google Pixel 9 Pro Fold (`comet` / Tensor G4 / `zumapro`)

> **検証状況**: GrapheneOS フルビルド（`user` バリアント）との組み合わせで実機ブート、KernelSU full モード（`lkm: true`, `late_load: false`）、および NixOS 26.05 (`systemd` PID 1) コンテナ + Termux-X11 GUI の稼働を実機で確認済みです。`0015` (ksud 自己復旧) は `user` 版 `2026112603` + relocked bootloader で、userdata wipe 直後の初回起動と Recovery からの無 wipe OTA (`adb sideload`) の両方で検証済みです。

## 構成

```
patches/aosp/     aosp/ 側パッチシリーズ (comet-nixos)
                  0001 GKI fragment + KMI 追加リスト (abi_gki_aarch64_comet_nixos)
                  0002 GKI fragment 整理 (namespace)
                  0003 USB デバイスドライバ =m 追加
                  0004 DRM prune / VT 有効化
                  0005-0009 KernelSU 組込み〜LKM 化 (v3.3.0, uapi2, KSU_VERSION 32601)
                  0010 shmem evict の still-mapped folio free 抑止 + ratelimit
                  0011 irq affinity_hint 読み出しの fault-tolerant 化
                  0012 CONFIG_CFI_PERMISSIVE=y
                  0013-0014 DRM ログ ratelimit / コメント調整
                  0015 ksud 自己復旧 (init.rc フックで /system/bin/ksud → /data/adb/ksud)
                  0016 udmabuf を VM_PFNMAP + vmf_insert_pfn 化 (upstream 7d79cd784470 の backport)
patches/device-os/ GrapheneOS OS 側 (vendor/google_devices/comet): ksud を /system/bin/ksud として同梱するための Android.bp と comet.mk 追記行
patches/device/   private/devices/google/comet 側 (fragment filegroup 登録 + device.bazelrc)
patches/kleaf/    NixOS ホストビルド対応 (hermetic_tools.bzl, ddk_uapi_headers.bzl, build-tools cp)
nixos/            nixos_gki.fragment (namespace, cgroup pids, tmpfs xattr/POSIX ACL, KSU 等)
ksu/              drivers/kernelsu (tgid != 1 hook 対応 + ksud 自己復旧込み) + kernel/ksu_module_exports.c
```

## 主なカーネル拡張ポイント

- **NixOS systemd コンテナ要件**: `CONFIG_TMPFS_XATTR=y`, `CONFIG_TMPFS_POSIX_ACL=y`, `CONFIG_CGROUP_PIDS=y`, `CONFIG_PID_NS=y`, `CONFIG_USER_NS=y` 等を有効化。
- **KernelSU 方案 C′ (`tgid != 1` 対応)**:
  - `ksu/kernelsu-src/core/init.c` で `tgid != 1` でも KSU hook 初期化が走るように調整。
  - `kernelsu.ko` を `system_dlkm` ではなく `vendor_kernel_boot`（`vendor_kernel_boot.modules.load`）から初期読み込みし、full モード（`late_load: false`）で動作。
- **`.ko` モジュール署名**: ビルド後の未署名 `.ko` は該当ビルドの `scripts/sign-file` と `certs/signing_key.{pem,x509}` で再署名して `comet-kernels` に反映。
  - **注意**: kleaf はカーネルをビルドするたびに `certs/signing_key.*` を新規生成します (bazel-out の `kernel_aarch64/certs/` にのみ存在)。ビルド直後に `certs/signing_key.{pem,x509}` と `scripts/sign-file` を bazel-out の外へ退避し、署名前に「退避した x509 が dist の `Image` 内に埋め込まれているか」を確認してください。別ビルドの鍵で署名すると lockdown 下で insmod が EPERM になり起動しません。
- **ksud 自己復旧 (userdata wipe 対応, `0015`)**: `user` 版 + relocked 端末で userdata wipe 直後の初回起動から KernelSU を有効にします。詳細 (失敗した試みの内訳、OS 側の組み込み方、署名鍵の罠) は [docs/ksud-self-restore.md](docs/ksud-self-restore.md) を参照。
  - OS 側に `ksud` を `cc_prebuilt_binary` として `/system/bin/ksud` に同梱 (`patches/device-os/`)。
  - カーネル注入の `on post-fs-data` 冒頭で、`ksu` ドメインの `exec` として `toybox cp -n` で `/data/adb/ksud` へ復元し、`toybox chcon` で `ksu_file` へ relabel してから `ksud post-fs-data` を起動。

- **udmabuf 起因の kernel panic 対処 (`0016`)**: GTK4-X11 + llvmpipe の Ghostty が `/dev/udmabuf` を mmap すると、アプリ終了頃に `Bad page map` → `list_del corruption` → panic していました。upstream の `VM_PFNMAP` 化を backport して根治します。原因解析・却下した代替案・検証手順は [docs/udmabuf-panic-fix.md](docs/udmabuf-panic-fix.md) を参照。**`2026112604` として OTA 生成済みですが、実機適用はまだ (2026-11-27 時点)**。
- **パッチシリーズの脱落に注意**: `2026112601`〜`2603` のビルドでは `0010`/`0011`/`0014` がツリーに入っていませんでした (`git status` には KSU 系しか出ないため気付けない)。ビルド前に dist の `vmlinux` の目印文字列 (`leaking; ratelimited` 等) を確認してください。

## ビルド (NixOS ホスト)

NixOS ホストでは kleaf の `hermetic_tools` が `PATH` を切り離すため、`patches/kleaf/hermetic_tools-nixos-host.patch` と適切な `bwrap` 環境を用いてビルドします。

```bash
# 1. GrapheneOS kernel tree を取得 (branch 17) し、パッチと ksu ソースを適用
#    aosp:         git am patches/aosp/*.patch
#    device/kleaf: git apply patches/device/*.patch patches/kleaf/*.patch
#    ksu:          ksu/kernelsu-src -> aosp/drivers/kernelsu
#                  ksu/ksu_module_exports.c -> aosp/kernel/
#                  nixos/nixos_gki.fragment -> private/devices/google/comet/
# 2. ビルド (scmversion 解決のため KLEAF_REPO_MANIFEST を指定)
./tools/bazel build --lto=full     KLEAF_REPO_MANIFEST=aosp_manifest.xml     --config=comet //private/devices/google/comet:zumapro_comet_dist
```

## 注意事項

- `0008` で追加する `kernel/ksu_module_exports.c` のシンボルは、ツールチェイン更新等で strict KMI チェックに引っかかる場合、`android/abi_gki_aarch64_*` への追記が必要になります。
- `0008` の `flask.h` / `av_permissions.h` は SELinux policy 更新時に `scripts/selinux/genheaders` との整合を確認してください。
- `0012` (`CONFIG_CFI_PERMISSIVE=y`) は vendor module との CFI hash 不一致を許容するための設定です。全 vendor モジュールを同一ビルド系で揃えた場合は無効化を検討できます。

## ライセンス

- カーネル側差分: GPL-2.0-only（上流 Linux カーネルに準拠）
- KernelSU (`ksu/kernelsu-src`): 上流 KernelSU のライセンスに準拠

## ライセンスに関する補足

- `ksud` バイナリは KernelSU 由来です。ライセンスは上流 KernelSU に準拠します。本リポジトリはバイナリを頒布せず、組み込み方 (`Android.bp` / `comet.mk`) のみを収録しています。バイナリは使用する `kernelsu.ko` と同じバージョンの公式 release から入手してください。
