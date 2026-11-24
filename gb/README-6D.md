# GB6D — Canon EOS 6D Game Boy / Game Boy Color

这是给 Canon EOS 6D（ML 6D.116）的 GB/GBC 模块，核心使用 [Walnut-CGB](https://github.com/Mr-PauI/Walnut-CGB)（MIT，单头文件、DMG/GBC、性能优先）。

## libretro 架构

本模块已经改成 libretro 风格的 host / core 分层：

```text
gb6d.c        host：菜单、ROM、BMP 3×、ASIF、6D 按键
retro_gb.c      core 适配层：实现 retro_* ABI
gb_core.c       Walnut-CGB 后端
walnut/         Walnut-CGB + minigb APU
```

host 只调用标准接口：

```c
retro_set_environment(...);
retro_set_video_refresh(...);
retro_set_audio_sample_batch(...);
retro_set_input_poll(...);
retro_set_input_state(...);
retro_init();
retro_load_game(&game);
while (running) retro_run();
retro_unload_game();
retro_deinit();
```

核心通过 `video_refresh` 输出 160×144 RGB332（6D 扩展像素格式，直接对应 BMP 调色板），通过 `audio_sample_batch` 输出 48kHz 立体声，通过 `input_state` 读取 `RETRO_DEVICE_ID_JOYPAD_*`。

后续加 NES/SMS 等机种时，只需要替换 `retro_*.c` 适配层和对应核心，host 的菜单、显示、音频、按键不用重写。

## 安装

```text
ML/MODULES/gb.mo
ML/GB/*.gb
ML/GB/*.gbc
```

`ML/GB/` 最多列出 32 个 ROM，按字母序。ROM 需要是合法的 Game Boy / Game Boy Color 文件；商业 ROM 请使用自己合法拥有的副本。

从 `Games → GB → Start GB` 启动。

## 6D 操作

| 6D 按键 | GB 动作 |
| --- | --- |
| 方向键 / 多功能控制器 | D-pad，支持斜方向 |
| `SET` | A |
| `PLAY` | B（140ms 脉冲） |
| 按住放大键（Magnify） | B（按住，适合跑/跳） |
| `INFO` | Select |
| `MENU` | Start |
| `Q` | 退出 |

说明：

- 6D 只发统一的 `UNPRESS_UDLR (0x36)`，方向保持到统一松开；
- `MENU` 作为 Start 被 GB 消费，运行中不能再打开 ML 菜单；退出用 `Q`；
- 放大键在 GB 运行期间只作为 B，不执行 Canon 放大。

## 显示与音频

- 160×144 的 GB 画面按 **3×** 放大到 480×432，居中显示（x=120, y=24）；
- 调色板使用 RGB332（256 色），GBC 颜色为近似值；
- GB APU 输出 48 kHz 单声道，走 Canon ASIF-DMA；
- `Games → GB` 菜单可设 `Audio` ON/OFF 和 `Volume` 1–10（6D 最大 10）。

## 构建

```sh
cp -r gb6d magiclantern/modules/gb6d
cd magiclantern
make -C modules/gb6d -j4
```

输出：

```text
modules/gb6d/build/gb.mo
```

`GB_6D=1` 会定义 `GB_6D`，启用 6D 原始键码。

## 已构建产物

```text
build/gb.mo   107196 bytes
build/gb.dep  578 bytes
build/gb.sym  2295 bytes
```

- 工具链：xPack GNU Arm Embedded GCC 15.2.1
- `gb.mo` SHA-256：`984F3E6093F9C00F4A8FD71FB73B9290CBEE48271488E5FE2BF566F66B0B0B4A`
- `gb.dep` 共 43 个未解析符号，在 6D.116 的 `6D_116.sym` 中全部找到（0 missing）
- 主机构建已验证：Walnut-CGB 加载 `dmg-acid2.gb`，连续 30 帧帧缓冲非零
- **尚未在 6D 实机运行**：显示/音频/按键需要实机确认

## 复杂特效优化

最新一轮日志：平均 **58.8fps**，普通场景 core 约 **12.9ms**，复杂特效峰值约 **26ms**。

已做：

- `WALNUT_GB_HIGH_LCD_ACCURACY=0`；
- CGB 64 项 RGB332 调色板 LUT；
- `gb_draw_frame` 每帧只做一次 3× 行复制；
- 关闭 16-bit 快速路径（6D 会黑屏）。

如果复杂特效仍掉帧，模块会自动进入短时 LCD 跳帧：CPU/APU 仍按 60fps 运行，只跳过部分 LCD 行绘制，避免整体掉到 40fps。`-Os` 构建实测更慢，已不再提供。

## 性能诊断

打开 `Debug logging` 后，`ML/LOGS/GB6D.LOG` 每 300 帧会写一行：

```text
PERF frames=... fps=... core=总耗时/峰值 display=总耗时/峰值 audio_under=... audio_over=...
```

- `core` 是 Walnut-CGB 一帧的模拟耗时；
- `display` 是 3× 放大和写 BMP 的耗时；
- `audio_under` 是音频回调取不到样本的次数；
- `audio_over` 是音频环形缓冲被覆盖的次数。

Walnut-CGB 和 minigb_apu 都使用 `-O3`；CGB 调色板用 64 项 RGB332 LUT 预计算；关闭 `WALNUT_GB_HIGH_LCD_ACCURACY` 降低复杂特效时的 PPU 开销。音频为 48kHz 立体声，混成单声道后写入 ASIF。

> 注意：`WALNUT_GB_16_BIT_OPS` / `WALNUT_GB_16_BIT_OPS_DUALFETCH` 在 6D/ARMv5 上会导致黑屏，已回退；不要再在 6D 构建里打开。

## 已知限制

- 3× 居中画面，左右有黑边；没有做 4× 裁切模式；
- GBC 颜色用 RGB332 近似，不是精确的 CGB 色板；
- 未接电池存档和即时存档；
- SGB 边框未使用；
- 部分 ROM 的 Nintendo logo 被修改过时会被 ROM 扫描拒绝。

## 许可

模块、Walnut-CGB 和 minigb_apu 均为 MIT；许可证分别见 `LICENSE-Walnut-CGB`、`LICENSE-minigb-apu`。







