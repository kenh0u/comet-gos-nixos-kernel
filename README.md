# comet-gos-nixos-kernel — GrapheneOS (Pixel 9 Pro Fold / comet) 向け KSU + NixOS コンテナ拡張カーネル差分

GrapheneOS 公式カーネルツリー（branch `17`, `6.1.177-android14-11`）に対して、KernelSU (LKM) と NixOS systemd コンテナ（`barec`）稼働に必要なカーネル構成・パッチをまとめた差分リポジトリです。

対象デバイス: Google Pixel 9 Pro Fold (`comet` / Tensor G4 / `zumapro`)

> **検証状況**: GrapheneOS フルビルド（`user` バリアント `2026112600`）との組み合わせで実機ブート、KernelSU full モード（`lkm: true`, `late_load: false`）、および NixOS 26.05 (`systemd` PID 1) コンテナ + Termux-X11 GUI の稼働を実機で確認済みです。

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
patches/device/   private/devices/google/comet 側 (fragment filegroup 登録 + device.bazelrc)
patches/kleaf/    build/kernel/kleaf/hermetic_tools.bzl の NixOS ホストビルド対応
nixos/            nixos_gki.fragment (namespace, cgroup pids, tmpfs xattr/POSIX ACL, KSU 等)
ksu/              drivers/kernelsu (tgid != 1 hook 対応込み) + kernel/ksu_module_exports.c
```

## 主なカーネル拡張ポイント

- **NixOS systemd コンテナ要件**: `CONFIG_TMPFS_XATTR=y`, `CONFIG_TMPFS_POSIX_ACL=y`, `CONFIG_CGROUP_PIDS=y`, `CONFIG_PID_NS=y`, `CONFIG_USER_NS=y` 等を有効化。
- **KernelSU 方案 C′ (`tgid != 1` 対応)**:
  - `ksu/kernelsu-src/core/init.c` で `tgid != 1` でも KSU hook 初期化が走るように調整。
  - `kernelsu.ko` を `system_dlkm` ではなく `vendor_kernel_boot`（`vendor_kernel_boot.modules.load`）から初期読み込みし、full モード（`late_load: false`）で動作。
- **`.ko` モジュール署名**: ビルド後の未署名 `.ko` は該当ビルドの `scripts/sign-file` と `certs/signing_key.{pem,x509}` で再署名して `comet-kernels` に反映。

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
