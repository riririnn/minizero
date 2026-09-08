# どうぶつしょうぎ環境

3×4 盤の将棋（考案：北尾まどか）。ResTNet 論文の 9×9 囲碁に相当する
「アーキテクチャ比較用の小さい環境」として追加した。

## 1. ルール

| 駒 | 記号 | 動き |
|---|---|---|
| ライオン | `L` | 8方向に1マス |
| きりん | `G` | 縦横4方向に1マス |
| ぞう | `E` | 斜め4方向に1マス |
| ひよこ | `C` | 前に1マス |
| にわとり | `H` | 斜め後ろ以外の6方向に1マス |

- 相手のライオンを取るか、**自分のライオンが相手の一段目に入れば勝ち**。
  ただし入った位置が相手の利きにあれば勝ちにならない
- 取った駒は自分の手番で空きマスのどこにでも打てる
- ひよこは相手の一段目に入ると**必ず**にわとりになる。
  にわとりを取っても、手駒はひよこに戻る
- 同一局面が3回現れたら引き分け
- 反則は無い（打ちひよこ・二ひよことも可）

初期配置（大文字が先手）。点対称になっている。

```
   a b c
 4 g l e     ← 後手の一段目
 3 . c .
 2 . C .
 1 E L G     ← 先手の一段目
```

## 2. なぜ将棋ではなく breakthrough を手本にしたか

将棋の実装は sunfish 由来のビットボード・利きテーブル一式（25ファイル・26万行）に
依存している。9路盤・11,259手を高速に扱うための機構で、12マス・132手の
どうぶつしょうぎには過剰。`breakthrough`（2ファイル・478行）の型を使った。

**決め手は特徴量の作り方**だった。MCTS は value が先手視点で来ることを前提に、
節点ごとに符号を反転している。

```cpp
// minizero/actor/mcts.cpp — getNormalizedMean
value = (action_.getPlayer() == charToPlayer(config::actor_mcts_value_flipping_player) ? -value : value);
```

将棋は特徴量を手番で180度回転させるため、ネットワークが学ぶ value は手番視点になり、
この規約と食い違う。実際それが原因で value が学習できていない
（`mdfolder/known_bugs.md`）。**盤を回転させなければこの問題は起きない**ので、
`breakthrough` や囲碁と同じく回転しない作りにした。

| | breakthrough流（採用） | shogi流 |
|---|---|---|
| ファイル数 | 2 | 本体2 + sunfish 25 |
| 盤面表現 | 配列12マス | ビットボード |
| 合法手生成 | 132手を全部試す | 専用生成器 + 14MB のテーブル |
| 行動ID | AlphaZero 形式を直接使う | 内部IDからの変換層 |
| 特徴量 | 回転なし | 手番で180度回転 |
| MCTS の規約 | **一致** | 食い違う |

## 3. 設計

### 盤面

`Piece{ PieceType, Player }` の配列12個。`owner_ == kPlayerNone` が空きマス。
`position = row * 3 + col` で、**row 0 が後手の一段目**、row 3 が先手の一段目。

先手（`kPlayer1`）は row の小さい方へ進む。

### 行動空間 132

```
盤上の手  12マス × 8方向                  =  96   action_id 0..95
打つ手    3種(きりん/ぞう/ひよこ) × 12マス =  36   action_id 96..131
```

ひよこの成りは到達で自動なので、成りフラグは持たない。

`PieceType` は**打てる駒を先頭に並べてある**ので、手駒の添字がそのまま
打つ手の行動IDに対応する。

```cpp
enum class PieceType { kGiraffe = 0, kElephant = 1, kChick = 2, kHen = 3, kLion = 4 };
```

方向は北から時計回りの8方向（`kDirectionRow` / `kDirectionCol`）。
盤面座標での定義なので、駒の動きを判定する `canReach()` の中で後手の行を反転させている。

```cpp
// canStep は先手側から見た定義。後手はここで行を反転する
return canStep(piece.type_, piece.owner_ == Player::kPlayer1 ? dr : -dr, dc);
```

行動の文字列表記は `a1b2`（移動）と `C*b2`（打つ手）。列は a〜c、行は先手側から 1〜4。

### 特徴量 18チャンネル

```
 0- 4  自分の駒 5種
 5- 9  相手の駒 5種
10-12  自分の持ち駒 3種（枚数を盤面全体に敷き詰める）
13-15  相手の持ち駒 3種
   16  先手番
   17  後手番
```

**盤面は回転させない。** 自分と相手はチャンネルの割り当てで区別し、
手番は専用の平面で与える。`breakthrough` と同じ方式。

非正方形なので `getInputChannelHeight()=4` / `getInputChannelWidth()=3` を上書きする
（前例: `dotsandboxes`）。

### 終局

```cpp
Player winner_;            // ライオンを取った/入った時点で確定させる
bool is_repetition_draw_;  // 同一局面3回
```

盤面から毎回計算し直すのではなく、**決まった瞬間に記録する**。
トライ勝ちは「入った位置が相手の利きにあるか」を `isAttacked()` で確かめてから確定させる。

千日手は局面文字列（盤面 + 両者の持ち駒 + 手番）の履歴を数える。

## 4. 検証

初期局面の合法手が **4手**（既知の値と一致）であることを Python で独立に確認した。

```
ひよこ 前進（相手ひよこを取る） / ライオン 左前・右前 / きりん 前
```

ぞうは斜め前が自分のひよこで塞がれているため動けない。

## 5. 既知の制約

- **相対位置バイアスが正方形盤を前提**にしているため、3×4 では
  `block_unit.py` の2行を直さないと実行時エラーになる（`mdfolder/known_bugs.md` の #2）
- `getActionFeatures()` は未実装（`breakthrough` と同じく MuZero 用。AlphaZero では使わない）

## 6. ビルド

```bash
scripts/build.sh dobutsu release
```

登録箇所は2つ。

```
minizero/environment/CMakeLists.txt   include に dobutsu を追加
minizero/environment/environment.h    型定義と env_board_size の既定値（3）
```

`scripts/build.sh` の対応ゲーム一覧は CMakeLists から自動生成されるので変更不要。
