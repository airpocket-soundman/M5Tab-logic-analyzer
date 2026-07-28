# シリアル制御 API

USB CDC (書き込みに使うのと同じ COM ポート、115200 bps) が制御 API を兼ねています。

- リクエストは **1 行**: `verb key=value key=value\n`
- レスポンスは **JSON 1 行**: 必ず `{"ok":true` か `{"ok":false` で始まる
- 1 リクエストにつき 1 レスポンス。途中に非同期出力は入りません

ログ出力は `CORE_DEBUG_LEVEL=1` に絞ってありますが、ESP-IDF が起動時などに
数行出すことがあります。**`{` で始まらない行は読み飛ばしてください。**

> **注意: DTR / RTS を操作しないこと。** ESP32 の USB-Serial-JTAG ではこの 2 本が
> リセットとブートピンに直結しており、トグルするとボードが ROM ブートローダに
> 落ちて応答しなくなります。ポートを開くときは両方 false のままにしてください。

コマンドは取り込みの**合間**に処理されます。取り込み中 (`capture()` 実行中) は
ブロックするため、掃引の途中で送った `stop` は次の掃引の前に効きます。

---

## コマンド一覧

### `ping`
疎通確認とファームウェア情報。

```json
{"ok":true,"fw":"0.1.0","api":1,"board":22,"channels":8,"parlio_built":true,"cpu_mhz":360}
```

### `status`
全状態のスナップショット。多くのコマンドはこれと同じ形を返します。

主なフィールド:

| キー | 内容 |
| --- | --- |
| `state` | `idle` / `sampling` / `done` / `failed` |
| `engine` / `engine_sel` | 実際に使われているエンジン / 選択設定 (`AUTO`/`PARLIO`/`CPU`) |
| `engine_note` | PARLIO が使えなかった場合の理由 |
| `rate_req` / `rate_actual` | 要求レート / 実効レート (Hz) |
| `depth` / `samples` | 設定深度 / 実際に取り込んだサンプル数 |
| `trigger` | トリガが見つかったサンプル位置。`-1` は未検出 |
| `capture_mode` | `direct ...` / `stream, copy ISR NN% busy ...` |
| `lossless_depth` | リアルタイム制約なしで取り込める最大サンプル数 |
| `trig_cond` | 8ch 分のトリガ条件 |
| `chan` | ch ごとの `pin` / `on` / `inv` |
| `view` | `start` (左端サンプル), `spp` (1px あたりサンプル), `w` (プロット幅 px) |
| `mem` | PSRAM / 内部 RAM の空き |

### `config [rate=<Hz>] [depth=<samples>] [engine=auto|parlio|cpu]`
`rate` は**メニュー内の最も近い値にスナップ**します (UI と設定が食い違わないため)。
`depth` は 2 の冪に切り上げ、64 kSa 〜 8 MSa にクランプされます。
深度を変えると現在の取り込みは破棄されます。レート変更では破棄されません。

`status` と同じ形を返します。

### `trigger [mode=auto|normal|single] [pos=0..95] [timeout=<ms>] [chN=off|rise|fall|both|high|low] [clear=1]`
エッジ条件どうしは OR、レベル条件どうしは AND、両方あるときは
「いずれかのエッジ AND すべてのレベル」。

```
trigger clear=1 mode=normal pos=25 ch0=rise ch1=low
```

### `run` / `single` / `stop`
`run` は連続取り込み、`single` は 1 回。いずれも `status` を返します。
実際の取り込みは次のループで走るので、完了は `status` の `state` をポーリングして
判定してください。

### `channel n=<0-7> [on=0|1] [inv=0|1]`
表示のオンオフと論理反転。反転は波形・測定・エクスポート・`edges` すべてに効きます。

### `decode kind=off|uart|i2c|spi [...]`
設定を変えると自動で再デコードされます。

| デコーダ | パラメータ |
| --- | --- |
| `uart` | `line=<ch>` `baud=<n>\|auto` `bits=5..9` `parity=n\|e\|o` `stop=1\|2` `invert=0\|1` `order=lsb\|msb` |
| `i2c` | `scl=<ch>` `sda=<ch>` `acks=0\|1` |
| `spi` | `clk=<ch>` `mosi=<ch>` `miso=<ch>` `cs=<ch>` `cpol=0\|1` `cpha=0\|1` `order=msb\|lsb` `bits=4..16` |

`mosi` / `miso` / `cs` は `-1` で無効化できます。

```json
{"ok":true,"decoder":"uart","ann":312,"truncated":false}
```

### `stats`
ch ごとの測定値。

```json
{"ok":true,"sec_per_sample":3.75e-08,"samples":2097152,"stats":[
  {"ch":0,"edges":157286,"rising":78643,"freq":1000000,"duty":47.5,
   "min_high":1.75e-07,"min_low":1.875e-07,"high_ratio":0.475}, ...]}
```

`freq` は立ち上がり間隔の平均から求めるので、窓の端で切れたバーストに引きずられません。
`high_ratio` は非周期的な信号でも意味を持つ生の High 比率です。

### `ann [from=<i>] [count=<n>]`
デコード結果。1 回あたり最大 512 件。

```json
{"ok":true,"decoder":"i2c","total":840,"from":0,"ann":[
  {"s":1024,"e":1025,"k":"info","r":0,"t":"START"},
  {"s":1100,"e":1900,"k":"addr","r":0,"t":"50 W"}, ...]}
```

`k` は `info` / `data` / `addr` / `ack` / `nak` / `err`、`r` は表示行、
`s` / `e` は開始・終了サンプル位置です。

### `edges ch=<0-7> [from=<sample>] [count=<n>]`
**自動化から波形を読むときの第一候補です。** 遷移位置だけを返すので、生サンプルより
桁違いに小さくなります。1 回あたり最大 2048 遷移。

```json
{"ok":true,"ch":0,"from":0,"level":1,"sec_per_sample":3.75e-08,
 "edges":[13,27,40,54,...],"next":50000,"more":true}
```

`level` は `from` 時点のレベル。`more` が `true` なら `next` を `from` にして続きを取得します。

### `read [from=<sample>] [count=<n>]`
生サンプルを 16 進で。1 サンプル 1 バイト、bit N が ch N。1 回あたり最大 4096 サンプル。

```json
{"ok":true,"from":0,"n":256,"hex":"fffefdfc..."}
```

### `view [fit=1] [zoom=<factor>] [center=<sample>] [trig=1]`
表示範囲の操作。`zoom` は 1 より大きいと拡大。

```json
{"ok":true,"start":1024.00,"spp":16.000000,"w":1184,"span_samples":18944.0}
```

### `cursor [a=<sample>] [b=<sample>] [clear=1]`

```json
{"ok":true,"a":1000,"b":2000,"dt":3.75e-05,"freq":26666.7}
```

### `gen ch=<0-7> | pin=<gpio> [freq=<Hz>] [duty=0..100]` / `gen off=1`
**内蔵テスト信号発生。** 指定したピンを LEDC で駆動します。

- `ch=0..7` — 測定チャンネルのピンを駆動します。駆動しながら同じピンを
  サンプリングできるので、**配線なしで**全経路 (GPIO マトリクス → PARLIO →
  DMA → コピー → バッファ) を検証できます。
- `pin=<gpio>` — **任意の GPIO** を駆動します。空きピン (例: G47 = M5-Bus 23番)
  を測定チャンネルにジャンパで直結すれば、**パッド → 実配線 → パッド**という
  外部経路まで含めて検証できます。同時に 4 本まで駆動できます。

```json
{"ok":true,"gen":"on","pin":47,"channel":-1,"freq":10000,"duty":50,"res_bits":11}
```

`channel` は駆動したピンが測定チャンネルでもある場合にその番号、そうでなければ `-1`。

`res_bits` は LEDC の分解能で、周波数が高いほど小さくなります。デューティの
量子化幅は `1/2^res_bits` なので、高い周波数では 50% ちょうどは出ません
(例: `res_bits=5` なら 15/32 = 46.9%)。

### `save what=vcd|csv|dec|png`
microSD へ書き出し。カードは自動でマウントされます。

```json
{"ok":true,"path":"/la/cap0000.vcd"}
```

---

## 使用例

### 動作確認 (配線不要)

```
gen ch=0 freq=1000000 duty=50
trigger clear=1
config engine=parlio rate=26666666 depth=2097152
single
```
`status` の `state` が `done` になるまでポーリングしてから:
```
stats
```
`stats[0].freq` が 1000000 前後になれば全経路が正常です。

### I2C バスを覗く

```
config rate=10000000 depth=1048576
trigger clear=1 mode=normal pos=10 ch0=fall
decode kind=i2c scl=0 sda=1
single
```
完了後:
```
ann count=64
```

### PowerShell からのヘルパー

```powershell
$p = New-Object System.IO.Ports.SerialPort "COM6",115200,None,8,one
$p.DtrEnable = $false   # 必須: トグルするとブートローダに落ちる
$p.RtsEnable = $false
$p.NewLine = "`n"
$p.Open()

function Send-Cmd([string]$c) {
  $p.WriteLine($c)
  $sb = New-Object System.Text.StringBuilder
  $deadline = (Get-Date).AddSeconds(15)
  while ((Get-Date) -lt $deadline) {
    $null = $sb.Append($p.ReadExisting())
    foreach ($ln in ($sb.ToString() -split "`n")) {
      $t = $ln.Trim()
      if ($t.StartsWith('{') -and $t.EndsWith('}')) { return $t | ConvertFrom-Json }
    }
    Start-Sleep -Milliseconds 15
  }
}

Send-Cmd 'ping'
```

## エラー

```json
{"ok":false,"err":"ch must be 0..7"}
```

| メッセージ | 意味 |
| --- | --- |
| `unknown command` | 動詞が未知 |
| `line too long` | 1 行 191 文字を超えた |
| `no capture` | まだ取り込みがない (`edges` / `read`) |
| `nothing captured` / `no card / mount failed` | microSD 側の問題 |
