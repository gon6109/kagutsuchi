# Kagutsuchi

Kagutsuchiは、C++とVulkanを使った物理シミュレーション基盤です。再利用可能なライブラリモジュールと、それらを利用する複数のアプリケーションで構成します。

## ディレクトリ構成

```text
lib/       再利用可能なライブラリモジュール
app/       アプリケーション（サブディレクトリごとに実行ファイルを定義）
cmake/     共通のCMake補助ファイル
assets/    サンプルなどで共有するアセット
docs/      設計・開発ドキュメント
```

各ライブラリモジュールは、それぞれのディレクトリに公開ヘッダーとソースを配置します。公開ヘッダーは、たとえば `lib/physics/include/kagutsuchi/physics/` に置きます。テストは対象モジュールの `lib/<module>/tests/` に配置します。モジュールやアプリを追加したら、親ディレクトリの `CMakeLists.txt` から `add_subdirectory()` で登録してください。

## 必要なツール

- CMake 3.24以降
- Ninja
- C++20に対応したコンパイラ

Vulkan SDKなどの依存関係は、必要になるモジュールを追加する段階で設定します。

## 構成とビルド

リポジトリのルートで次のコマンドを実行します。

```sh
cmake --preset ninja
cmake --build --preset ninja
```

ビルド出力先は `build/` です。アプリケーションは既定でビルド対象です。モジュールのテストを有効にする場合は、構成時に `-DKAGUTSUCHI_BUILD_TESTS=ON` を指定してください。
