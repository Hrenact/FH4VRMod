# FH4VRMod

为 **Steam 版《极限竞速：地平线 4》**开发的实验性 3DoF VR 模组，通过 SteamVR / OpenVR 输出双眼画面。

当前版本：**v0.1.0-preview.2 — 地图裁切修正版**。上一版 `v0.1.0-preview.1` 继续保留为独立恢复点。

## 已验证的功能

- 交替双眼渲染、立体深度及头部旋转跟踪。
- 座舱与暂停菜单车辆背景的立体画面；已实际体验更换车辆、LEGO 谷地、比赛及场景切换。
- F2 持续开关 VR，无 60 秒限时。
- F6 只校准左右朝向；正常抬头、低头和歪头跟踪仍保留。
- UI 固定在开启或回正时的前方，文字及玩家头像列表的已知裁切问题已改善。
- VR 中自动修正暂停菜单地图裁切，无需 F11；完整地图缩放、平移、F2 重复开关及驾驶小地图检查均已通过。
- 从当前 SteamVR 读取推荐尺寸和双眼投影；不固定为某款头显或某个分辨率。

验证环境为 RTX 4070、PICO 4 Pro、PICO Connect 无线串流与 SteamVR。其他设备尚未验证。

## 兼容范围

当前相机和 UI 接口对应 Steam FH4 **1.478.564.0 / build 12772551**。

已验证的 `ForzaHorizon4.exe` SHA-256：

```text
b838a79a65c6d1e12f46deaba3584e4c88ebccdfdc4214b4535ece4ba3c0abbe
```

Microsoft Store 版及不同游戏版本尚未适配。本模组需要用户自行安装游戏，不包含游戏文件。

## 安装

1. 下载 [Releases](https://github.com/Hrenact/FH4VRMod/releases) 中的 Windows x64 运行包，正常退出 FH4。
2. 在 Steam 的“管理 → 浏览本地文件”打开游戏目录。
3. 如果目录中已有 `dxgi.dll`，先将它和已有的 `fh4vr` 目录、`fh4vr.ini` 备份到游戏目录之外。不要直接覆盖其它模组的 DLL；本版未提供代理链加载。
4. 将运行包中的 `dxgi.dll`、`fh4vr` 文件夹及两个 `Prepare-VRResolution` 脚本复制到 `ForzaHorizon4.exe` 所在目录。`fh4vr.ini.example` 是可选配置示例，不会覆盖现有配置。
5. 连接头显并启动 SteamVR。游戏关闭时运行 `Prepare-VRResolution.cmd`，它会读取当前头显参数，在 `fh4vr.ini` 中生成一个额外的游戏分辨率选项；已有 INI 会先备份。
6. 从 Steam 启动 FH4。在“视频 → 基本”切换窗口模式，选择脚本生成的分辨率并保存，关闭动态模糊。
7. 进入座舱并停车，戴好头显、朝前按 F2 开启 VR。

当前沉浸式路径直接读取 SteamVR 姿态，**不需要运行旧版 pose_bridge**。改变头显、SteamVR 渲染比例或 RenderScale 后，应在下次启动游戏前重新运行尺寸配置脚本，并在游戏内选择新尺寸。

## 按键

| 按键 | 功能 |
| --- | --- |
| F2 | 开启 / 关闭 VR；开启时以当前左右朝向回正 |
| F6 | 仅回正左右朝向，并将固定 UI 放回前方 |
| F9 | 将当前游戏输出截图保存到游戏目录；每个交换链最多 3 张，通常每次启动游戏可拍 3 张 |

游戏需保持前台；切出游戏会停止当前 VR 输出，回到游戏后再按一次 F2 开启。F9 抓图可能短暂停顿，达到上限后可重启游戏再拍。F1、F3、F4、F5、F7、F8、F10 等保留的开发测试键不属于正常使用流程。

## 清晰度设置

在游戏目录的 `fh4vr.ini` 中添加或修改以下配置；如果还没有 INI，可从 `fh4vr.ini.example` 创建。保留脚本已生成的其它配置：

```ini
[VR]
RenderScale=1.0
```

这是相对于 SteamVR 推荐尺寸的**宽高线性比例**，允许 0.25–2.0；0.75 表示宽和高分别为 75%。此项本身不会自动修改游戏实际分辨率，需要重新运行配置脚本并在游戏中选择生成的选项。脚本对当前复制路径的尺寸上限进行检查，超限时可降低比例。

## 已知限制

- 驾驶 HUD 的 Enter 键盘按键图标背景在部分转头角度仍会裁切，当前保留此限制；用户实测同一位置的 Xbox 手柄图标正常。
- 这是 3DoF 旋转跟踪，不提供头部平移的 6DoF 支持。
- 交替双眼渲染的有效双眼更新率低于游戏帧率，快速运动时仍可能出现时序差异。
- 诊断日志保留在游戏目录，长时间运行会增加磁盘占用；F9 图片也保存在本地。
- 只验证了上述游戏版本和一套头显环境，尚未进行大范围兼容性测试。

游戏内的目的地标识、玩家名片及车辆仪表不能一概按普通屏幕 UI 处理，后续修复会继续区分这些路径。

## 恢复与卸载

1. 如要卸载，先在游戏中恢复显示器支持的普通分辨率并保存。然后正常退出 FH4。
2. 要回到本测试基线，从 `v0.1.0-preview.1` Release 重新下载运行包，核对同名 `.sha256` 附件及包内的 `SHA256SUMS.txt`，恢复其中的 `dxgi.dll` 与 `fh4vr` 文件。
3. 要卸载本模组，移走本模组的 `dxgi.dll`、`fh4vr` 目录及配置脚本；若安装前有其它代理 DLL，恢复此前备份。不要删除其它模组的文件。
4. 恢复先前的 `fh4vr.ini` 备份，或移走本模组新建的 INI；重新启动游戏并选择常用分辨率、窗口/全屏设置。

这个恢复点的主 DLL SHA-256：

```text
11161B2FD0042A832C539179E8B2D2A546ED7CF933451F4D218ACAF53563DF2B
```

## 从源码构建

需要 Windows x64、Visual Studio C++ x64 工具链、Windows SDK，以及 Git。

```powershell
git clone --recurse-submodules https://github.com/Hrenact/FH4VRMod.git
cd FH4VRMod
git switch --detach v0.1.0-preview.2
git submodule update --init --recursive
cmd /c build-stereo-trial.cmd
cmd /c build-runtime-mode.cmd
```

当前构建入口通过 `vswhere` 查找工具链。输出为 `build/stereo-trial/dxgi.dll` 和 `query_runtime_mode.exe`。重新构建可能因工具链和时间戳不同而产生不同的文件哈希；需要精确恢复时使用 Release 附件。

基本验证：

```powershell
.\build\stereo-trial\test_ui_material_unlit_gate.exe
.\build\stereo-trial\test_ui_material_clip.exe
.\build\stereo-trial\test_ui_map_probe.exe
.\build\stereo-trial\test_graphics_probe.exe "$PWD\build\stereo-trial\dxgi.dll"
.\build\stereo-trial\test_runtime_mode.exe
```

`src` / `tools` 还保留早期实验与本地研究工具，部分旧入口包含开发环境路径；发布构建和用户安装按本文步骤进行。需要游戏内存样本、提取 shader 或截图的离线研究测试不会随源码附带这些输入，不能当作普通单元测试直接运行。

当前 Release 主 DLL 为用户实际测试过的原文件。发布准备仅调整测试、构建入口和文档，不更改其运行时逻辑。

## 第三方依赖

- [Valve OpenVR](https://github.com/ValveSoftware/openvr)，固定提交 `0924064316de3effbcd1acf1e309182a2deb1c05`。运行包附其许可证。
- [MinHook](https://github.com/TsudaKageyu/minhook)，固定提交 `c3fcafdc10146beb5919319d0683e44e3c30d537`，保留供早期研究构建使用；当前虚表拦截版本不链接它。

不分发游戏可执行文件、内存导出或从游戏提取的 shader。开发早期的相机签名调查参考过 [ForzaMods 相机源码](https://github.com/ForzaMods/Forza-Mods-AIO/blob/447030847c136d0ef9fcc3c2d7ded2c0d13eb5bd/Forza-Mods-AIO/Forza-Mods-AIO/Cheats/ForzaHorizon4/CameraCheats.cs)；本项目的扫描、拦截和投影实现独立编写。
