# NES6D — Canon EOS 6D / QuickNES

这是 [lauris-nl/nes6d](https://github.com/lauris-nl/nes6d) 的 6D 版本，但**核心已替换为 [QuickNES](https://github.com/kode54/QuickNES_Core)**（GPL-2.0）。

原来的自制 NES 核心只支持 Mapper 0/1/4，兼容性和 CPU/PPU/APU 行为有限；QuickNES 是成熟的 NES 模拟器核心，支持绝大多数常见 Mapper（NROM、UxROM、CNROM、MMC1、MMC3、MMC5、VRC、FME-7 等），带完整 APU 和成比例音频输出。

本目录保留 NES6D 的 ML 外壳（菜单、ROM 扫描、720×480 显示、ASIF-DMA 音频、6D 按键队列），只把 `nes_core_*` 后面的核心换掉。

## 6D 操作

| 6D 按键 | NES 动作 |
| --- | --- |
| 方向键 / 多功能控制器 | D-pad，支持斜方向 |
| `SET` | A（按下/松开） |
| `PLAY` | B（140ms 脉冲） |
| 按住放大键（Magnify / Zoom In） | B（按住，适合跑/连发） |
| `INFO` | Select |
| `MENU` | Start |
| `Q` | 退出 NES |

说明：

- 6D 没有独立的 B 键，所以 `PLAY` 给 B 一个短脉冲；需要按住 B（例如马里奥跑动）时，用放大键。
- 6D 只发一个 `UNPRESS_UDLR (0x36)`，不是四个独立松开事件。方向按下后保持到统一松开，并支持 6D 的八个方向/斜方向码。
- `MENU` 作为 Start 被 NES 消费，运行中不能再打开 ML 菜单；退出用 `Q`。
- `SET` 的 A 键使用 press/release；`INFO`/`MENU`/`PLAY` 是脉冲。

## 音量

在 `Games → NES` 菜单里：

- `Audio` 必须为 `ON`；
- `Volume` 可调 `1` 到 `ASIF_MAX_VOL`（6D 为 10）；
- `1` 最轻，数值越大越响；`Audio = OFF` 为静音。

音量在 `Start NES` 时应用到 Canon 音频输出；如果已经进入游戏，先按 `Q` 退出，改完再重新启动。

## 构建

把本目录复制到魔灯源码树：

```sh
cp -r nes6d magiclantern/modules/nes6d
cd magiclantern
make -C modules/nes6d -j4
```

生成：

```text
modules/nes6d/build/nes.mo
```

必须带 `NES_6D=1`（会定义 `NES_6D`）。standalone 模块构建通常不会定义 `CONFIG_6D`；若在 6D 平台树中编译，`CONFIG_6D` 也会自动启用。

## 安装

```text
ML/MODULES/nes.mo
ML/NES/*.nes
```

- `ML/NES/` 最多列出 32 个 `.nes` 文件，按字母序。
- 使用 QuickNES 的 Mapper 支持，覆盖绝大多数常见 iNES 游戏；不支持的 Mapper 会明确报错。
- 不包含任何 ROM 或商业游戏数据，请使用自己合法拥有的 ROM。
- 6D 上使用 6D.116 的 `autoexec.bin` 和 `ML/modules/6D_116.sym`；不要使用 6D 的模块。

## 已构建的产物

```text
build/nes.mo   441608 bytes
build/nes.dep  682 bytes
build/nes.sym  24869 bytes
```

- 工具链：xPack GNU Arm Embedded GCC 15.2.1
- `nes.mo` SHA-256：`2B3A4B28F68F38BEF219018BEF2E763E5DA629E465BD5F805512CE8F5B708E0B`
- `nes.dep` 共 45 个未解析符号，在 6D.116 的 `6D_116.sym` 中全部找到（0 missing）
- 已确认编译进 6D 分支：`nes6d: NES menu registered (6D keymap)` 字符串
- QuickNES 的 Blip_Buffer 均衡器需要 `libm`；链接时已把 `libm`/`libgcc` 静态链进模块，不依赖相机导出 `cos/exp/pow`
- **尚未在 6D 实机运行**：核心替换、按键、符号依赖和编译已验证，显示/音频/性能仍需实机确认

## 核心替换验证

除了编译，另用 Windows 主机测试程序直接驱动同一个 QuickNES 核心：

- 加载一个自制的 NROM 测试 ROM（16 KiB PRG + 8 KiB CHR，非商业游戏）；
- 连续运行 10 帧；
- 输出 `256×240`、`pitch=272`、`palette_begin=0`、`palette_size=64`；
- 每帧音频约 `798–799` 个 48 kHz 样本；带 pulse 音的测试 ROM 有 `5581` 个非零样本；
- 第 10 帧有 `61440` 个非零像素，palette 索引在 0–63 范围内。

这验证了 QuickNES 的 ROM 读取、CPU/PPU、palette、帧缓冲和音频输出路径；6D 上的 ML 显示/音频/按键仍需实机验证。

## 渲染方式（60fps）

当前构建使用 `NES6D_LEGACY_RENDER=0`：

- `Video`: `Exact 2x`（512×480）或 `Stretch fullscreen`（720×480）；
- `Buffer`: 只保留 `Direct`，始终写可见 buffer。

注意：Direct 60fps 可能仍有撕裂；这是为了 60 帧做的取舍。

## 已知限制

1. 原 NES6D 的显示、`LCD_Palette`、FIO 和 ASIF-DMA 音频仍按 6D 编写；6D 符号全部存在，但实际颜色/声音/帧率需测试。
2. QuickNES 的 Mapper 覆盖比旧核心大得多，但不保证 100%；个别依赖特殊时序/CHR-RAM 的游戏仍可能有问题。
3. 无 PAL 时序、即时存档、电池存档（QuickNES 本身支持存档，但 NES6D 外壳暂未接）。
4. 6D 上 `MENU` 被映射为 Start，无法在游戏中使用 ML 菜单；`Q` 退出。
5. 放大键在 NES 运行期间只作为 B，不执行 Canon 放大；退出后恢复。

## 测试清单

- [ ] 四个方向各按/放，确认不粘键；
- [ ] 八方向斜按，确认不会卡住两个正交方向；
- [ ] `SET` = A，按住放大键 = B，`PLAY` 脉冲 B；
- [ ] `INFO` = Select，`MENU` = Start；
- [ ] `Q` 退出后 ML 菜单/屏幕恢复正常；
- [ ] 换不同 Mapper 的 ROM，确认能启动或给出明确的不支持提示；
- [ ] 检查声音是否正常、画面是否为 512×480 居中 2×；
- [ ] 打开 Debug logging，查看 `ML/LOGS/NES-*.LOG` 和 `NESRAW.LOG`。

## 许可

上游项目与 GPL-2.0 兼容；Doom/NES 商业 ROM 不随本项目分发。






