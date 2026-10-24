# Sandbox アプリケーション設計

## 目的

`sandbox` は、Kagutsuchi の物理シミュレーションや Vulkan 描画を試し、動作を観察するための実験用アプリケーションとする。再利用する価値が確認できた処理は、責務を整理したうえで `lib/` の独立モジュールへ移す。

この文書は現在の設計と実装状況を記録し、実装の進行に合わせて更新する。

## 実装状況

- マイルストーン1を実装済み。GLFWウィンドウの作成・イベント処理と、spdlogによる起動・終了・エラー記録を行う。
- マイルストーン2を実装済み。Vulkan instance、GLFW surface、swapchain 拡張を備え graphics/present queue に対応する device、swapchain、image view、clear 用 render pass/framebuffer、command buffer と描画同期を初期化する。フレームごとに暗い青色で画面をクリアして present する。
- framebuffer サイズ変更後は device の処理完了を待って swapchain 依存リソースを再作成する。最小化中は framebuffer が 0 でなくなるまでイベント待機し、終了要求も確認する。
- Dear ImGui の GLFW platform backend と Vulkan renderer backend を使い、Vulkan renderer、swapchain サイズ、image 数を状態パネルに表示する。ImGui の descriptor pool と backend は VulkanContext が所有する。
- GLSL vertex/fragment shader を `glslangValidator` でビルド時に SPIR-V 化する。VulkanContext は swapchain の render pass に適合する graphics pipeline を作成し、頂点バッファなしの三頂点描画で色付き三角形を描く。
- VS Code の sandbox 起動構成では作業ディレクトリを `build/app/sandbox/`（実行ファイルの配置先）にする。実行時ファイルはこの作業ディレクトリに生成される。
- swapchain 再生成時は device idle 後に ImGui backend と graphics pipeline/layout を終了し、render pass と swapchain image 数を更新してから pipeline と ImGui backend を再初期化する。終了時はそれらを swapchain と device より先に破棄する。shader module は pipeline 作成後に破棄し、SPIR-V ファイルだけを実行時に読む。
- 開発用 Validation Layers は `KAGUTSUCHI_SANDBOX_VALIDATION_LAYERS=ON` で有効化する。要求した layer が Vulkan SDK にない場合は起動時にエラーとする。

## 初期構成

- C++20、CMake、Ninja でビルドする。
- ウィンドウと入力には GLFW を使う。
- 描画 API は Vulkan とする。
- GUI とデバッグ操作には Dear ImGui を使い、GLFW platform backend と Vulkan renderer backend を利用する。
- ベクトル、行列、変換には GLM を使う。
- ログには spdlog を使う。
- シェーダーは GLSL で記述し、ビルド時に `glslangValidator` で SPIR-V に変換する。

GLFW、Dear ImGui、spdlog は CMake の `FetchContent` で取得する。取得するバージョンは CMake 設定で固定する。現在は GLFW 3.4、Dear ImGui 1.91.9b、spdlog 1.15.3 を取得する。GLM は後続の描画段階で追加する。Vulkan SDK は外部開発環境として導入し、Vulkan loader、ヘッダー、および `glslangValidator` を利用する。

## 責務の境界

- `app/sandbox/` はアプリケーションの起動、実験内容、sandbox 固有の UI と描画を持つ。
- 外部ライブラリは sandbox から直接利用し、最初の段階では外部ライブラリをまとめて隠すだけの汎用ラッパーを `lib/` に作らない。
- Vulkan 初期化、ウィンドウ統合、描画などで複数の利用箇所に共通する安定した責務が見つかった場合、その責務と API を定めてから `lib/` に切り出す。
- 物理シミュレーションの再利用可能な機能は、Vulkan や UI に依存させず、適切な `lib/<module>/` に置く。

## 描画とシェーダー

Vulkan が受け取るシェーダー形式は SPIR-V とする。ソース GLSL を実行時に直接読み込むのではなく、CMake のカスタムコマンドで `.vert`、`.frag` などを SPIR-V に変換し、その出力をアプリケーションが読み込む。

初期の描画機能は、ウィンドウの表示、Vulkan instance/device/surface/swapchain の作成、画面クリア、色付き三角形、および Dear ImGui による sandbox 状態表示までを実装済み。Validation Layers は開発時に有効化できる。

## 依存の配置とビルド成果物

外部依存のソースコードはリポジトリへ手動コピーせず、FetchContent の取得・展開先をビルドディレクトリ配下に置く。ビルド生成物と SPIR-V 出力も `build/` 配下に置く。アプリケーションのソース、ヘッダー、シェーダーは `app/sandbox/` 以下で管理する。

## 初期ディレクトリ案

```text
app/
  CMakeLists.txt
  sandbox/
    CMakeLists.txt
    docs/
      design.md
    src/
      main.cpp
    shaders/
      ...
```

実装が増えたら、Vulkan context、renderer、window、UI などの分割を必要に応じて追加する。初期段階では将来の構造を先回りして細分化しない。

## 初期マイルストーン

1. GLFW のウィンドウを作成し、spdlog で起動・終了を記録する。（実装済み）
2. Vulkan instance、device、surface、swapchain を初期化し、画面をクリアする。（実装済み）
3. GLSL シェーダーをビルド時に SPIR-V 化して、簡単な図形を描画する。（三角形描画を実装済み）
4. Dear ImGui の GLFW/Vulkan backend を接続し、sandbox の状態を表示する。（状態表示を実装済み。操作 UI は未実装）
5. GLM を使った変換と、物理シミュレーションの小さな実験を表示する。

各段階で、sandbox の CMake ターゲットと依存設定を更新する。依存や開発手順が追加された場合は、ルート README と `docs/development.md` にも必要な環境・手順を反映する。
