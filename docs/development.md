# 開発ガイド

## プロジェクト概要

Kagutsuchiは、C++とVulkanを使った物理シミュレーション基盤です。再利用可能なライブラリ（`lib/`）と、それらを利用する複数のアプリケーション（`app/`）で構成します。

## ディレクトリと構成ルール

- 各ライブラリは `lib/<module>/` に配置し、独立したCMakeターゲットとして定義します。
- 公開ヘッダーは各モジュールの `include/` に配置します。例: `lib/physics/include/kagutsuchi/physics/`。
- 実装ソースは各モジュールの `src/` に配置します。
- 各アプリケーションは `app/<application>/` に配置し、独立した実行ターゲットとして定義します。
- モジュールやアプリを追加したら、対応する親 `CMakeLists.txt` から `add_subdirectory()` で登録します。
- 共通のCMake補助ファイルは `cmake/`、設計資料は `docs/` に配置します。
- テストは対象モジュールの `lib/<module>/tests/` に配置し、そのモジュールのCMake設定から登録します。

## ビルド

CMake 3.24以降、Ninja、およびC++20対応コンパイラを使用します。標準の構成とビルドには次を使います。

```sh
cmake --preset ninja
cmake --build --preset ninja
```

アプリケーションは既定で有効です。モジュールのテストは `KAGUTSUCHI_BUILD_TESTS` オプションが有効な場合に構成し、CTestへ登録します。

## 設計・変更時の方針

- モジュール間の依存は必要最小限にし、物理計算とVulkan固有処理の責務を分けます。
- 新しい依存ライブラリやツールを導入するときは、CMake設定とREADMEの必要環境・手順を合わせて更新します。
