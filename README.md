# esp_dualarm_servo

ROS2（Raspberry Pi）から USB シリアル経由で ESP32 を制御し、ダブルアーム（左右各4軸・計8サーボ）の RC サーボを動かすシステム。

```
[ROS2 / Raspberry Pi]  --USB serial(115200)-->  [ESP32]  --PWM(50Hz)-->  [8x RC Servo]
   dualarm_bridge.py                          src/main.c                  左4 + 右4
```

ESP32 は「1行のテキストコマンド」を受け取り、LEDC で8チャンネルの PWM（パルス幅 500〜2500µs / 50Hz）を生成する。ラズパイ側は `JointState` を購読し、関節角を校正してシリアルに流すだけの薄いブリッジ。実機がなくてもエミュレータで全経路をテストできる。

---

## ハードウェア構成

- マイコン: ESP32（`esp32dev`、クラシック ESP32）
- サーボ: RC サーボ 8個（DS3218 クラスを想定）、左アーム4 + 右アーム4
- サーボ電源: 別電源（例: LiFePO4 2S）。ESP/USB からは給電しない

### ピンアサイン

| servo id | アーム | GPIO |
|:--:|:--:|:--:|
| 0 | 左 | 13 |
| 1 | 左 | 14 |
| 2 | 左 | 27 |
| 3 | 左 | 26 |
| 4 | 右 | 25 |
| 5 | 右 | 33 |
| 6 | 右 | 32 |
| 7 | 右 | 4 |

入力専用ピン(34–39)・ストラップピン(0,2,12,15)は出力に使わない。

### 配線の注意（重要）

- サーボ8個の合計突入/停動電流は大きい（DS3218 クラスは個別に数 A）。**必ず別電源**から太い線で給電する。
- **GND は ESP32 と外部電源で共通化**する。
- 電源ラインに大きめの電解コンデンサ（数百〜数千µF）を入れて電圧降下とリブートを防ぐ。

---

## 通信プロトコル

USB シリアル、115200 bps、改行（`\n`）区切りの ASCII。

| コマンド | 形式 | 例 | 意味 |
|:--|:--|:--|:--|
| 単体指定 | `<id> <angle>` | `3 120` | servo 3 を 120° へ |
| 一括指定 | `S a0 a1 … a7` | `S 90 80 100 95 90 85 100 90` | 8軸まとめて更新 |

- `angle` は度（0〜180）。範囲外はファーム側でクランプ。
- 応答: 成功で `OK 3 120.0` / `OK S`、解釈不能で `ERR [...]`。
- 一括指定は値が足りなければ残りの軸は現状維持。

---

## ディレクトリ構成（想定）

```
esp_dualarm_servo/
├── README.md
├── platformio.ini          # PlatformIO 設定（espidf / esp32dev）
├── src/
│   └── main.c              # ESP32 ファーム（LEDC 8ch + シリアル受信）
├── ros/
│   └── dualarm_bridge.py   # ROS2 ブリッジノード（JointState -> serial）
└── tools/
    ├── tester.py           # PC からの手動テスター
    └── emu.py              # ESP32 エミュレータ（実機なしテスト用）
```

---

## ESP32 ファームウェア

LEDC の低速グループ（クラシック ESP32 で8ch）を 50Hz の単一タイマに束ね、チャンネルごとのデューティでパルス幅を変える。

### ビルドと書き込み

PlatformIO:

```bash
pio run                 # ビルド
pio run -t upload       # 書き込み（ポート自動検出）
pio device monitor      # モニタ（115200）
```

素の ESP-IDF（`main/main.c` + `main/CMakeLists.txt` に `idf_component_register(SRCS "main.c" REQUIRES driver)`）:

```bash
idf.py set-target esp32           # 初回のみ
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor    # 抜けるのは Ctrl+]
```

書き込めない / `Connecting…` で止まる場合は、BOOT を押しながら EN(RST) を一度押して書き込みモードに入れる。

---

## ROS2 ブリッジノード（Raspberry Pi）

`sensor_msgs/JointState`（トピック `joint_states`）を購読し、関節名をサーボID順に並べ替え、ラジアン→度＋校正してから一括コマンド（`S …`）を送る。

### 校正

ROS の関節角（ラジアン）と実機サーボ角は原点・向き・範囲が異なるため、軸ごとに調整する。

```
servo_deg = OFFSET[i] + SIGN[i] * degrees(joint_position)
```

- `OFFSET[i]`: その関節の中立位置に対応するサーボ角（既定 90）
- `SIGN[i]`: 回転方向が逆なら -1
- `JOINT_ORDER`: サーボID 0..7 に対応する関節名（実機の命名に合わせる）

### 起動

```bash
ros2 run <pkg> dualarm_bridge --ros-args -p port:=/dev/ttyUSB0
```

依存: `rclpy`, `sensor_msgs`, `pyserial`。Docker 運用時はコンテナに `--device=/dev/ttyUSB0`（compose は `devices:`）を渡す。デバイス名が変わるなら `/dev/serial/by-id/...` を使う。

---

## 動かし方

### A. 実機あり

1. ファームを書き込む（上記）。
2. サーボ信号8本を GPIO に、サーボ電源を別電源に、GND を共通化、ESP32 の USB を PC/ラズパイに接続。
3. ポート確認: `ls /dev/ttyUSB*`
4. 送信側を起動:
   - 手動テスト: `python3 tools/tester.py /dev/ttyUSB0` → `3 120` や `S 90 80 100 95 90 85 100 90`
   - ROS: `ros2 run <pkg> dualarm_bridge --ros-args -p port:=/dev/ttyUSB0` ＋
     ```bash
     ros2 topic pub /joint_states sensor_msgs/msg/JointState \
       "{name: ['l_joint1'], position: [0.5]}"
     ```
5. サーボが動き、`OK ...` 応答が返れば成功。

推奨順: まず手動テスターで8軸の中立・可動範囲を確認 → `OFFSET`/`SIGN` を詰める → ROS に切り替える。

### B. マイコンなし（エミュレータ）

仮想シリアルのペアを作り、片方に送信側・もう片方にエミュレータを繋いで全経路を検証する。

```bash
# ターミナルA: 仮想シリアルのペア（起動したまま）
socat -d -d pty,raw,echo=0,link=/tmp/vesp_ros pty,raw,echo=0,link=/tmp/vesp_emu

# ターミナルB: ESP32 エミュレータ（受信側）
python3 tools/emu.py /tmp/vesp_emu

# ターミナルC: 送信側（どちらか）
python3 tools/tester.py /tmp/vesp_ros
# または
ros2 run <pkg> dualarm_bridge --ros-args -p port:=/tmp/vesp_ros
```

エミュレータは実機ファームと同じ規則でコマンドを解釈し、各サーボの角度とパルス幅[µs]を表示する。実機へ移すときは送信先を `/tmp/vesp_ros` → `/dev/ttyUSB0` に変え、`emu.py` を本物の ESP32 に置き換えるだけ。

JointState→文字列変換だけを確かめたい場合は、ノードの `serial.Serial` をモックに差し替えればシリアルすら不要。

---

## トラブルシューティング

| 症状 | 対処 |
|:--|:--|
| ポートが見えない / 書き込めない | `sudo usermod -aG dialout $USER` して再ログイン |
| `Connecting…` で止まる | BOOT を押しながら EN を一押し |
| 反応はあるが動かない・唸る | 電源容量と GND 共通化を確認 → `SERVO_MIN_US`/`MAX_US` を実機に合わせて調整 |
| 全軸の向き・原点がずれる | ROS ノードの `OFFSET`/`SIGN` を関節ごとに調整 |
| モニタが静かすぎる | ファームはログを `ESP_LOG_WARN` に下げている。動作確認は `tester.py` で |