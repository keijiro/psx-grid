# psx-grid

Jacquard の Score Plane を PlayStation のゲームパッドで編集する GUI 試作です。
起動時は空の 128×64 セルの平面を表示します。音声生成・再生・保存は行いません。

## セットアップと実行

Apple Silicon Mac、Xcode Command Line Tools、Homebrew、Git が必要です。
参照プロジェクト `../psx-test` と同じ PSn00bSDK v0.24、MIPS GCC 16.2.0 /
binutils 2.47、PCSX-Redux build 250 と同梱 OpenBIOS を使用します。
固定情報は [toolchain.lock](toolchain.lock) にあります。

```sh
./scripts/setup.sh
source scripts/env.sh  # zsh
cmake --preset debug
cmake --build --preset debug
./scripts/run.sh
```

Release:

```sh
source scripts/env.sh
cmake --preset release
cmake --build --preset release
./scripts/run.sh build/release/psx-grid.exe
```

SDK・エミュレーター・設定は `.local/`、SDK ソースは `third_party/` に配置します。
既存の固定バージョンのシステムツールチェーンを再利用します。セットアップは再実行可能です。
`env.sh` はシェル設定ファイルを書き換えません。
`run.sh` は interpreter / debugger を有効にし、ログを標準出力に送ります。
`PCSX_REDUX`、`PCSX_REDUX_BIOS`、`PCSX_REDUX_DATA` で起動先を変更できます。

初回にエミュレーターの自動更新設定が出たら選択を完了してください。
固定バージョンを保つ場合は自動更新を無効にします。
macOS が起動をブロックした場合は Finder からアプリを開いて確認してください。
エミュレーターの `Configuration > Controls` で Port 1 の D-pad / Cross / Circle を
ゲームパッドまたはキーボードに割り当てます。未接続時と再接続時のボタン解放待ちは編集を停止します。

## 操作

| 状態 | D-pad | X / Cross | ○ / Circle |
| --- | --- | --- | --- |
| 平面 | カーソル移動・端で自動スクロール | メニュー | なし |
| メニュー | 上下で選択 | 実行 | 閉じる |
| 長さ変更 | 左右で候補を変更 | 確定 | 取消 |
| レーン削除確認 | 左で取消、右で削除 | 確定 | 取消 |

1. 初期位置 `(1,1)` で X → `NEW LANE` を X で確定します。
2. 右へ移動し、X → `PLACE TILE` でタイルを置きます。
3. 同じ位置で X → `DELETE TILE` でタイルだけを消します。
4. `CHANGE LENGTH` で長さを変更します。黄色の枠が候補終端です。
   画面外の終端は端の矢印と `END` 座標で示します。○ で元データを維持して戻ります。
5. 緑の先頭へ戻り、`DELETE LANE` → 右 → X でレーンとタイルを削除します。

先頭は緑の四角、終端は橙色の縦線、通常タイルは青い四角、カーソルは白い枠です。
長押しは 18 フレーム後、以降 3 フレーム間隔（NTSC で約 300 ms / 50 ms）。
逆方向同時押しは相殺し、両軸では横方向を優先します。

最大 16 レーン、1〜64 ステップ（新規は16）。先頭・終端を含めて領域を占有します。
衝突・平面外・容量超過を拒否し、短縮でタイルが失われる場合は先に削除する案内を表示します。
メニューと取消操作ではカーソル位置を保持します。

## 構成と検証

- `src/score.*`: SDK 非依存モデルと編集検証。
- `src/input.*`: 押下・リピート・切断／再接続の処理。
- `src/editor.*`: 状態遷移、メニュー、長さ候補、削除確認。
- `src/render.*`: 320×240 NTSC、ダブルバッファ、スクロール、描画パケット管理。
- `src/main.c`: パッド読み取りとフレームループ。
- `build/{debug,release}/psx-grid.{elf,exe}`: ELF と PS-X EXE。

ホストの Clang で ASan / UBSan を使ったテストを実行できます。

```sh
./scripts/test.sh
```

境界・衝突・容量・取消・入力・反復編集に加え、GPU スタブで全座標・全画面状態の
描画範囲とバッファを確認します。GPU スタブは実機 GPU や操作感の検証には代わりません。
確認済み項目と残作業は [docs/validation.md](docs/validation.md) を参照してください。
