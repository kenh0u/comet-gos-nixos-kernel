# udmabuf 起因のカーネルパニック (Ghostty / llvmpipe) の修正 — `0016`

対象: GrapheneOS `user` 版 `2026112603` (kernel `6.1.177-android14-11`, `Image.lz4` md5 `c3486c0b…`) で、
**Ghostty を開いて約 2 分後に (終了時に) カーネルパニック**する問題。

- 発生: 2026-11-27 04:05 JST, uptime 10865s に Ghostty 起動 → 10979s に panic。
- 証拠: pstore `work/pstore-harvest/20261127-0405/pstore/console-ramoops-0` (host 側)。
- 修正: `patches/aosp/0016-comet-nixos-udmabuf-VM_PFNMAP-vmf_insert_pfn.patch`
- **検証状況: パッチ適用ビルド (`2026112604`, `Image.lz4` md5 `d88dcdb0…`) と OTA は生成済み。
  実機への sideload と Ghostty 起動/終了の確認はまだ (2026-11-27 時点で端末が手元に無いため未実施)。**

---

## 1. 症状 (pstore の時系列)

```
10865.49  .ghostty-wrappe  WARNING at drivers/dma-buf/dma-buf.c:843 dma_buf_mmap_internal+0x14c
          (以後 ~0.6s 間隔で連発。WARN_ON(!(vma->vm_flags & VM_SPECIAL)))
10979.03  llvmpipe-7  BUG: Bad page map ... refcount:0 mapcount:-1
                      file:dmabuf fault:udmabuf_vm_fault mmap:dma_buf_mmap_internal
                      (exit_mmap → unmap_page_range → print_bad_pte, 14953 件 suppress 込み)
10979.05  llvmpipe-7  BUG: Bad page state ... "non-NULL mapping"
                      aops:shmem_aops ino:c3 dentry:"memfd:shared-memory-region"
                      free_large_kmalloc ← release_udmabuf ← dma_buf_release ← __fput ← do_exit
10979.06  Thread-1    BUG: Bad page state (refcount:-2) ← release_pages ← zap_page_range_single ← madvise
10979.66  crash_dump64  list_del corruption. next->prev should be …, but was …
                      kernel BUG at lib/list_debug.c:64  __list_del_entry_valid ← get_populated_pcp_list
          Kernel panic - not syncing: Oops - BUG: Fatal exception
```

WARN 自体は良性 (`dma_buf_mmap_internal` が直後に `VM_DONTEXPAND` を付ける)。
実害はその後の「map されたままのページが解放される」連鎖で、buddy/pcp のリストが壊れて
どのプロセスのページ確保でも panic し得る状態になります。

## 2. 原因

Android 6.1 (`android14-6.1`) の `drivers/dma-buf/udmabuf.c`:

```c
static vm_fault_t udmabuf_vm_fault(struct vm_fault *vmf)
{
	...
	vmf->page = ubuf->pages[pgoff];
	get_page(vmf->page);
	return 0;
}
static int mmap_udmabuf(struct dma_buf *buf, struct vm_area_struct *vma)
{
	...
	vma->vm_ops = &udmabuf_vm_ops;
	vma->vm_private_data = ubuf;
	return 0;                   /* VM_PFNMAP 等を立てない */
}
```

1. udmabuf の中身は memfd (shmem) のページ。`udmabuf_create` が `shmem_read_mapping_page` +
   `get_page` で 1 参照持ち、`release_udmabuf` が `put_page` で返す。
2. `.fault` は `vmf->page` を返すので、`do_set_pte` は **通常の file-backed マッピング**として
   `page_add_file_rmap` を張る (mapcount++)。
3. 一方 VMA は `dma_buf_mmap()` の `vma_set_file(vma, dmabuf->file)` で **dmabuf の anon inode に
   付け替え**られる。つまり元 memfd の `address_space->i_mmap` には、この VMA は繋がっていない。
4. llvmpipe は `lp_dma_buf` / allocation 用の memfd を最後の fd close で unlink する。
   `shmem_evict_inode → shmem_undo_range` は `unmap_mapping_range` を **memfd の i_mmap に**対して
   行うが、手順 3 のため udmabuf を mmap した VMA には届かない。
5. その結果 `folio_mapped()` な folio が truncate され、`mm/filemap.c: filemap_unaccount_folio()` の
   「Bad page cache: still mapped when deleted」経路に入る。ここで **`mapping_exiting()` の救済 free**
   (`folio_ref_count >= mapcount + 2` なら `page_mapcount_reset` + `folio_ref_sub(mapcount)`)
   が、実際にはまだ map されている folio に対して走り、refcount/mapcount を実際より減らす。
6. 後でプロセスが exit/madvise で PTE を外すと `mapcount:-1` の bad pte になり、ページは
   二重解放される → buddy/pcp のリスト破壊 → panic。

`Bad page cache` の行自体はこの pstore のコンソールには残っていませんでした (WARN の
フラッドで ring buffer から押し出された期間に該当)。ただし手順 5 を裏付ける事実として:

- 2603 の `vmlinux` に `0010` の文字列 (`leaking; ratelimited`) が **無い** (0010 未適用で救済 free が有効)。
- `mapcount:-1` は `page_mapcount_reset` で 0 にされた後に `page_remove_rmap` が 1 減じた形に一致。
- `release_udmabuf` が put したページが `refcount:0` で "non-NULL mapping" になっている。

なお手順 5 の救済 free は 10 月の QUESTIONS #46/#50 (「shmem evict の still-mapped folio」) と
同じ箇所で、`0010` はそれを「救済 free をしない」ことで止める対症療法でした。
今回分かったのは、**その上流にある「udmabuf の VMA が memfd の i_mmap から外れている」**
が構造的原因で、これは `0010` では塞げません (ページは漏れるし WARN も残る)。

## 3. 選定した修正

upstream v6.11 の **`7d79cd784470` "udmabuf: use vmf_insert_pfn and VM_PFNMAP for handling mmap"**
(Vivek Kasireddy, 2024-06-24) をそのまま 6.1 に backport:

```c
	pfn = page_to_pfn(ubuf->pages[pgoff]);
	return vmf_insert_pfn(vma, vmf->address, pfn);
...
	vm_flags_set(vma, VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
```

- `VM_PFNMAP` になった VMA の PTE は special 扱い (`vm_normal_page()` が NULL を返す) になり、
  rmap も mapcount も張られない。ユーザ空間からは pfn を共有するだけなので、shmem 側の
  truncate と udmabuf の refcount が干渉しなくなる (**根治**)。
- `VM_SPECIAL` が立つので `dma_buf_mmap_internal` の WARN も消える。
- udmabuf のページ参照は `udmabuf_create`/`release_udmabuf` の 1 対で閉じるため、
  `VM_PFNMAP` でも use-after-free にはならない (VMA は `vm_file` で dmabuf を保持し、
  release は全 VMA 消滅後に来る)。

backport は hunks がそのまま当たりました (6.1 と v6.11 の当該関数は同一)。
`vmf_insert_pfn()` / `VM_DONTDUMP` は 6.1 にあります。

## 4. 検討して採らなかった方法

| 方法 | 却下理由 |
|---|---|
| Ghostty 側で udmabuf を回避 (`--gtk-gpu=2`, `GDK_DEBUG=no-gpu` 等) | 10 月の #49 で「GSK renderer / `gtk-gpu-renderer` / `LIBGL_DRI3_DISABLE` のどれでも BUG 件数が変わらない」と実測済み。llvmpipe が常時使う経路で、ユーザ空間側のスイッチでは回避できない。 |
| `0010` (救済 free 廃止) のみ再適用 | panic は止まる見込みだが、folio が漏れ続ける (mapcount 分の ref が残る)、WARN は残る、X server の SHM 等 udmabuf 以外の経路には効果が無い。対症療法。 |
| `release_udmabuf` で mapped なページを put しない | VMA が `vm_file` を保持している以上、release が mapped 中に来ることは本来無い。来ているように見えたのは手順 5 で refcount が不当に減ったためで、udmabuf 側は書き換える必要が無い。 |
| `dma_buf_mmap()` で `vma_set_file` をやめる | dmabuf の VMA が元 inode の `i_mmap` に残るようにする大改造。全 exporter (system heap, CMA heap, vendor) の VMA の意味が変わり、`unmap_mapping_range` の対象も変わる。影響範囲が読み切れない。 |
| 同シリーズの `c6a3194c05e7` (memfd_pin_folios 化) も取り込む | 6.1 に `memfd_pin_folios()` が無い。foll を自前で用意する必要があり、本件の解決に必須でない。 |
| GKI 以外の vendor モジュール (mali 等) の udmabuf 利用への影響 | vendor は udmabuf を mmap していないことを前提にしている (Android の udmabuf は主に仮想デバイス用)。ただし **これは実機で mali/DRM が正常であることを以て確認すべき** で、未確認。 |

## 5. あわせて行ったこと

- **`0013` は未適用のまま**: 2603 のツリーに無いことは `verify-kernel.sh` で確認しましたが、ログの大量出力を抑えるだけのパッチなので今回は見送りました (次回 `git am` で入れること)。
- **`0010` + `0014` を再適用**: 2603 のツリーから脱落していた (`vmlinux` に文字列が無いことで確認)。
  udmabuf 以外の経路 (X server の SHM など) への保険として残す。
- **`0011` を再適用**: 同じく脱落していた (`/proc/irq/*/affinity_hint` の fault-tolerant 化)。
- `0001`-`0004`, `0012` は fragment / `BUILD.bazel` / fragment 側で反映済みのため触っていません (`0004` の DRM prune の WARN 抑制は `drm_mode_prune_invalid` の `verbose` 扱いで入っています)。

**教訓**: 2601〜2603 は `git status` で KSU 系の変更しか見えないツリーからビルドされており、
`patches/aosp/` のシリーズが全部は入っていませんでした。ビルド前に
「dist の `vmlinux` に各パッチの目印文字列があるか」を確認する運用を推奨します。

## 6. ビルド・検証手順

```bash
# カーネル (NixOS ホスト)。詳細は README「ビルド」。
#   patches/aosp/0016-*.patch を git am で適用後に通常どおり kleaf ビルド。

# 反映確認 (目印):
#   新: udmabuf_vm_fault が vmf_insert_pfn_prot を呼ぶ / mmap_udmabuf が w9 に 0x400 (VM_PFNMAP) を立てる
llvm-objdump -d vmlinux --start-address=0x$(nm vmlinux | awk '/ t udmabuf_vm_fault$/{print $1}') | grep -m1 'bl.*vmf_insert_pfn'
#   0010: strings vmlinux | grep -c 'leaking; ratelimited'   → 1
```

実機での確認項目 (未実施):

1. Ghostty を起動 → 2 分以上使う → 終了。`dmesg | grep -c dma_buf_mmap_internal` が 0。
2. `dmesg | grep -c 'Bad page'` が 0 (起動以来)。
3. その後 10 分以上放置しても panic しない (パニックはアプリ終了〜数秒後に来る)。
4. Chrome (aarch64) / i3 / rofi が従来どおり動く。
5. 可能なら glmark2 等で llvmpipe の描画が崩れないことを目視。

## 7. ホスト環境で詰まった点 (カーネルの内容とは無関係)

`patches/kleaf/` に 2 件追加しました。いずれも NixOS ホスト + btrfs に起因します。

- `ddk_uapi_headers-HOSTLDFLAGS-to-implicit-rule.patch`:
  `make -C ${KERNEL_DIR} -f /dev/null scripts/unifdef` は `Makefile` を読まないため
  `KBUILD_HOSTLDFLAGS` が未定義で、make の暗黙ルールが `CC=clang` を **`LDFLAGS` なし**で
  呼ぶ → `crtbeginS.o` / `-lgcc` が見つからず失敗 (Ubuntu 等 `/usr/lib/gcc` が常に見える
  環境では顕在化しない)。`LDFLAGS="$HOSTLDFLAGS" CFLAGS="$HOSTCFLAGS"` を明示的に渡します。
- `build-tools-cp-toybox-ACL-EINVAL-wrapper.patch`:
  hermetic tools の `cp` (toybox) が `cp -L -p` で ACL をコピーしようとして EINVAL。
  "preserving permissions" で失敗した場合のみ `-p` を外して再実行するラッパーに置き換えます。

真因は **`/home/llm` に付いている btrfs の default ACL (`default:user:kenhou:rwx`)** で、
bazel サンドボックス / bwrap 内では `kenhou` が unmapped になり `setxattr` が
`EINVAL` になります。`grapheneos-kernel-6.1/` と GOS ツリー全体 (含む `out/`) から
`setfacl -R -k` + `setfacl -R -x u:kenhou` で除去しました。ただし `/home/llm` 自体の
default ACL は残しているため、**新規に `git repo init` したディレクトリ等では再発します**。
カーネルのビルドはこの 2 パッチが無くても、ACL が無い環境では通りません (未検証)。

## 8. 参照

- upstream: <https://github.com/torvalds/linux/commit/7d79cd7844709b7c7f38f1dc5b6b91ea8e8b7b16>
- LKML: <https://lkml.kernel.org/r/20240624063952.1572359-6-vivek.kasireddy@intel.com>
- 前身の調査: `20260923-lineage/QUESTIONS.md` #46 (2026-10-01), #49, #50
