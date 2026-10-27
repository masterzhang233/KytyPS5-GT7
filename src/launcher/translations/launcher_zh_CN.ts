<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>Configuration</name>
    <message>
        <location filename="../src/configuration.cpp" line="+160"/>
        <source>Unknown game setting: %1</source>
        <translation>未知的游戏设置：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Game setting %1 must be a string.</source>
        <translation>游戏设置 %1 必须是字符串。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Invalid value for game setting: %1</source>
        <translation>游戏设置值无效：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>User name must contain 1-16 UTF-8 bytes.</source>
        <translation>用户名必须包含 1–16 个 UTF-8 字节。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>GPU index must be -1 or greater.</source>
        <translation>GPU 索引必须为 -1 或更大。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Vblank frequency must be between 30 and 360.</source>
        <translation>垂直消隐频率必须介于 30 和 360 之间。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Enabled logging and command buffer dumps require an output path.</source>
        <translation>启用日志或命令缓冲区转储时必须指定输出路径。</translation>
    </message>
</context>
<context>
    <name>ConfigurationEditDialog</name>
    <message>
        <location filename="../forms/configuration_edit_dialog.ui" line="+14"/>
        <source>Settings</source>
        <translation>设置</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>User profile</source>
        <translation>用户配置</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>User name:</source>
        <translation>用户名：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Local user name exposed to games</source>
        <translation>提供给游戏的本地用户名</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>User ID:</source>
        <translation>用户 ID：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Local user ID exposed to games</source>
        <translation>提供给游戏的本地用户 ID</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Console language:</source>
        <translation>主机语言：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Language</source>
        <translation>语言</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Launcher language:</source>
        <translation>启动器语言：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Interface language; applies after restarting the launcher</source>
        <translation>界面语言；重启启动器后生效</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Audio</source>
        <translation>音频</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Microphone:</source>
        <translation>麦克风：</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Graphics &amp;&amp; display</source>
        <translation>图形与显示</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Screen resolution:</source>
        <translation>输出分辨率：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Window resolution</source>
        <translation>窗口输出分辨率</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Present mode:</source>
        <translation>呈现模式：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Vulkan swapchain presentation mode</source>
        <translation>Vulkan 交换链呈现模式</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Vblank frequency:</source>
        <translation>垂直消隐频率：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Virtual display refresh rate used for frame pacing</source>
        <translation>用于帧节奏控制的虚拟显示刷新率</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Shader optimization:</source>
        <translation>着色器优化：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Optimize shaders for code size or performance</source>
        <translation>按代码大小或性能优化着色器</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>GPU:</source>
        <translation>GPU：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Graphics device used by the emulator</source>
        <translation>模拟器使用的图形设备</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Run in borderless desktop fullscreen</source>
        <translation>以无边框桌面全屏模式运行</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fullscreen</source>
        <translation>全屏</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Read back writable linear images</source>
        <translation>回读可写线性图像</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enable readback</source>
        <translation>启用回读</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Draw tessellated geometry. Disabled by default; unsupported tessellation may cause crashes.</source>
        <translation>绘制曲面细分几何体。不受支持的曲面细分可能导致崩溃。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enable tessellation support (experimental)</source>
        <translation>启用曲面细分支持（实验性）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Hide the host cursor after 2 seconds without mouse activity</source>
        <translation>鼠标静止 2 秒后隐藏主机光标</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Auto-hide cursor</source>
        <translation>自动隐藏光标</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Sample heap-indexed textures through bindless descriptor arrays instead of enumerating them per draw.</source>
        <translation>通过无绑定描述符数组采样堆索引纹理，避免每次绘制逐一枚举。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bindless textures (experimental)</source>
        <translation>无绑定纹理（实验性）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Redraw the reflection cube maps (the reflections on cars) every other frame instead of every frame. Faster; the reflections lag one frame.</source>
        <translation>每隔一帧重绘反射立方体贴图（车身反射），而非每帧重绘。速度更快，但反射会延迟一帧。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reflections every other frame (faster)</source>
        <translation>隔帧更新反射（更快）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Skip shaders:</source>
        <translation>跳过着色器：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Guest shader hashes (hex, comma-separated) whose draws and dispatches are left out</source>
        <translation>要跳过绘制和分派的客体着色器哈希（十六进制，逗号分隔）</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Notifications</source>
        <translation>通知</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Show trophy unlock notifications and play their sound.
To use your own sound, replace the emulator&apos;s assets/sounds/trophy-unlock.wav with a WAV file and restart the game.</source>
        <translation>显示奖杯解锁通知并播放提示音。
如需使用自定义声音，请将模拟器的 assets/sounds/trophy-unlock.wav 替换为 WAV 文件并重启游戏。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Enable trophy notifications</source>
        <translation>启用奖杯通知</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>GT7 internal rendering / SteamVR (experimental)</source>
        <translation>GT7 内部渲染 / SteamVR（实验性）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Desktop 3D height:</source>
        <translation>桌面模式 3D 高度：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Sets GT7&apos;s internal scene scale, separately from Screen resolution. Heights are nominal; special passes can differ. Native keeps the original controller.</source>
        <translation>设置 GT7 的内部场景尺寸，与输出分辨率相互独立。高度为标称值，特殊渲染通道可能不同；原生选项会保留游戏原始控制方式。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>SteamVR / Quest</source>
        <translation>SteamVR / Quest</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connect the headset in SteamVR before starting. Uses the game&apos;s stereo rendering and the full-frustum compatibility patch.</source>
        <translation>启动前请先在 SteamVR 中连接头显。使用游戏的立体渲染和完整视锥兼容补丁。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Internal size per eye; independent of SteamVR&apos;s resolution setting. Higher values use more GPU time and memory.</source>
        <translation>单眼内部渲染尺寸，与 SteamVR 的分辨率设置相互独立。数值越高，占用的 GPU 时间和显存越多。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>PPSA01317 01.710.000 only. The executable fingerprint is verified before launch. Game files remain unchanged.</source>
        <translation>仅支持 PPSA01317 01.710.000。启动前会验证可执行文件指纹，游戏文件不会被修改。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Debugging &amp;&amp; logs</source>
        <translation>调试与日志</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Enable Vulkan validation layers</source>
        <translation>启用 Vulkan 验证层</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Vulkan validation</source>
        <translation>Vulkan 验证</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Validate SPIR-V binary</source>
        <translation>验证 SPIR-V 二进制文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shader validation</source>
        <translation>着色器验证</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Enable RenderDoc capture</source>
        <translation>启用 RenderDoc 捕获</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>RenderDoc capture</source>
        <translation>RenderDoc 捕获</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Dump command buffers</source>
        <translation>转储命令缓冲区</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Command buffer dump</source>
        <translation>命令缓冲区转储</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Shader logging:</source>
        <translation>着色器日志：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Dump shaders to file or console window. If enabled may decrease emulator performance</source>
        <translation>将着色器转储到文件或控制台窗口。启用后可能降低模拟器性能</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Shader log folder:</source>
        <translation>着色器日志文件夹：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Specify directory to dump shaders</source>
        <translation>指定着色器转储目录</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Printf output:</source>
        <translation>Printf 输出：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Print logs to file or console window. If enabled may decrease emulator performance</source>
        <translation>将日志输出到文件或控制台窗口。启用后可能降低模拟器性能</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Printf output file:</source>
        <translation>Printf 输出文件：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Specify file to dump logs</source>
        <translation>指定日志输出文件</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Enable the Tracy profiler. Profiling may decrease emulator performance</source>
        <translation>启用 Tracy 分析器。性能分析可能降低模拟器性能</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Tracy profiler</source>
        <translation>Tracy 分析器</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Command buffer folder:</source>
        <translation>命令缓冲区文件夹：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Specify directory to dump command buffers</source>
        <translation>指定命令缓冲区转储目录</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Compatibility</source>
        <translation>兼容性</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Patch CPU instructions that differ between AMD and the host CPU.</source>
        <translation>修补 AMD 与主机 CPU 之间存在差异的 CPU 指令。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>AMD CPU patch (experimental)</source>
        <translation>AMD CPU 补丁（实验性）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Helps prevent crashes in some games on Windows</source>
        <translation>帮助防止部分游戏在 Windows 上崩溃</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Windows SysV red zone crash protection (experimental)</source>
        <translation>Windows SysV 红区崩溃保护（实验性）</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Controller</source>
        <translation>控制器</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>DualSense lightbar:</source>
        <translation>DualSense 灯条：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Choose and preview a fixed LED color. Bluetooth DualSense may take several seconds to update.</source>
        <translation>选择并预览固定 LED 颜色。蓝牙连接的 DualSense 可能需要数秒才能更新。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Custom</source>
        <translation>自定义</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Let the game control the lightbar</source>
        <translation>由游戏控制灯条</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Default</source>
        <translation>默认</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Vibration intensity:</source>
        <translation>振动强度：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Scale DualSense vibration over USB and Bluetooth</source>
        <translation>调整 USB 和蓝牙 DualSense 的振动强度</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>100%</source>
        <translation>100%</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Speaker volume:</source>
        <translation>扬声器音量：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Scale the DualSense speaker over USB and Bluetooth. 50% is the previous default; 100% adds 6 dB of gain.</source>
        <translation>调整 USB 和蓝牙 DualSense 的扬声器音量。50% 是之前的默认值，100% 会增加 6 dB 增益。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>50%</source>
        <translation>50%</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Reset</source>
        <translation>重置</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Restore default settings</source>
        <translation>恢复默认设置</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Game Configs</source>
        <translation>游戏配置</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Import or export game configs</source>
        <translation>导入或导出游戏配置</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Save</source>
        <translation>保存</translation>
    </message>
    <message>
        <location filename="../src/configurationEditDialog.cpp" line="+68"/>
        <source>Japanese</source>
        <translation>日语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>English (United States)</source>
        <translation>英语（美国）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>French (France)</source>
        <translation>法语（法国）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Spanish (Spain)</source>
        <translation>西班牙语（西班牙）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>German</source>
        <translation>德语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Italian</source>
        <translation>意大利语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Dutch</source>
        <translation>荷兰语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Portuguese (Portugal)</source>
        <translation>葡萄牙语（葡萄牙）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Russian</source>
        <translation>俄语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Korean</source>
        <translation>韩语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chinese (Traditional)</source>
        <translation>繁体中文</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chinese (Simplified)</source>
        <translation>简体中文</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Finnish</source>
        <translation>芬兰语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Swedish</source>
        <translation>瑞典语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Danish</source>
        <translation>丹麦语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Norwegian</source>
        <translation>挪威语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Polish</source>
        <translation>波兰语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Portuguese (Brazil)</source>
        <translation>葡萄牙语（巴西）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>English (United Kingdom)</source>
        <translation>英语（英国）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Turkish</source>
        <translation>土耳其语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Spanish (Latin America)</source>
        <translation>西班牙语（拉丁美洲）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Arabic</source>
        <translation>阿拉伯语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>French (Canada)</source>
        <translation>法语（加拿大）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Czech</source>
        <translation>捷克语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hungarian</source>
        <translation>匈牙利语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Greek</source>
        <translation>希腊语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Romanian</source>
        <translation>罗马尼亚语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Thai</source>
        <translation>泰语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Vietnamese</source>
        <translation>越南语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indonesian</source>
        <translation>印度尼西亚语</translation>
    </message>
    <message>
        <location line="+55"/>
        <source>DualSense lightbar color</source>
        <translation>DualSense 灯条颜色</translation>
    </message>
    <message>
        <location line="+12"/>
        <location line="+3"/>
        <source>%1%</source>
        <translation>%1%</translation>
    </message>
    <message>
        <location line="+69"/>
        <source>Fifo</source>
        <translation>先进先出</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Mailbox</source>
        <translation>邮箱</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Immediate</source>
        <translation>立即</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+41"/>
        <source>None</source>
        <translation>无</translation>
    </message>
    <message>
        <location line="-39"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Performance</source>
        <translation>性能</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Silent</source>
        <translation>静默</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Console</source>
        <translation>控制台</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>File</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Microphone used by games. None supplies silence.</source>
        <translation>游戏使用的麦克风。“无”将提供静音输入。</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Microphones could not be listed: %1</source>
        <translation>无法列出麦克风：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1 (unavailable)</source>
        <translation>%1（不可用）</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Auto</source>
        <translation>自动</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Native (unchanged)</source>
        <translation>原生（不修改）</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>100% - 2400 x 2040 / eye</source>
        <translation>100% - 单眼 2400 × 2040</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>110% - 2640 x 2244 / eye</source>
        <translation>110% - 单眼 2640 × 2244</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>120% - 2880 x 2448 / eye</source>
        <translation>120% - 单眼 2880 × 2448</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>125% - 3000 x 2550 / eye</source>
        <translation>125% - 单眼 3000 × 2550</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Game folders</source>
        <translation>游戏文件夹</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Add game folder</source>
        <translation>添加游戏文件夹</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Remove selected game folders</source>
        <translation>移除选中的游戏文件夹</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>简体中文</source>
        <translation>简体中文</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>English</source>
        <translation>English</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Import...</source>
        <translation>导入…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Export...</source>
        <translation>导出…</translation>
    </message>
    <message>
        <location line="+108"/>
        <location line="+6"/>
        <location line="+6"/>
        <source>Save failed</source>
        <translation>保存失败</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Please fill all mandatory fields</source>
        <translation>请填写所有必填字段</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>User name must contain 1-16 UTF-8 bytes</source>
        <translation>用户名必须包含 1–16 个 UTF-8 字节</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>User ID cannot be 254 (everyone) or 255 (system)</source>
        <translation>用户 ID 不能为 254（所有人）或 255（系统）</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Select game folder</source>
        <translation>选择游戏文件夹</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Select game folders</source>
        <translation>选择游戏文件夹</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Add</source>
        <translation>添加</translation>
    </message>
</context>
<context>
    <name>ConfigurationItem</name>
    <message>
        <location filename="../src/configurationItem.cpp" line="+53"/>
        <location line="+11"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="-9"/>
        <source>Main menu</source>
        <translation>主菜单</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>In game</source>
        <translation>游戏中</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Logo</source>
        <translation>标志画面</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Doesn&apos;t boot</source>
        <translation>无法启动</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Serial ID: %1</source>
        <translation>序列号：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Game version: %1</source>
        <translation>游戏版本：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Firmware version: %1</source>
        <translation>固件版本：%1</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Path: %1</source>
        <translation>路径：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Comment: %1</source>
        <translation>备注：%1</translation>
    </message>
</context>
<context>
    <name>ConfigurationListWidget</name>
    <message>
        <location filename="../forms/configuration_list_widget.ui" line="+14"/>
        <source>Configurations</source>
        <translation>配置</translation>
    </message>
    <message>
        <location line="+64"/>
        <source>&lt;html&gt;&lt;head/&gt;&lt;body&gt;&lt;p&gt;Edit global settings&lt;/p&gt;&lt;/body&gt;&lt;/html&gt;</source>
        <translation>&lt;html&gt;&lt;head/&gt;&lt;body&gt;&lt;p&gt;编辑全局设置&lt;/p&gt;&lt;/body&gt;&lt;/html&gt;</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Edit input mapping</source>
        <translation>编辑输入映射</translation>
    </message>
    <message>
        <location line="+25"/>
        <location filename="../src/configurationListWidget.cpp" line="+797"/>
        <source>Edit game config</source>
        <translation>编辑游戏配置</translation>
    </message>
    <message>
        <location line="+25"/>
        <location filename="../src/configurationListWidget.cpp" line="+27"/>
        <location line="+371"/>
        <source>Clear game config</source>
        <translation>清除游戏配置</translation>
    </message>
    <message>
        <location line="+53"/>
        <source>Search game name or serial</source>
        <translation>搜索游戏名称或序列号</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Search name or serial</source>
        <translation>搜索名称或序列号</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Game</source>
        <translation>游戏</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Serial</source>
        <translation>序列号</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Game Version</source>
        <translation>游戏版本</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Firmware Version</source>
        <translation>固件版本</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Path</source>
        <translation>路径</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Comments</source>
        <translation>备注</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Refresh game list</source>
        <translation>刷新游戏列表</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Rescan game folders for new games</source>
        <translation>重新扫描游戏文件夹以查找新游戏</translation>
    </message>
    <message>
        <location filename="../src/configurationListWidget.cpp" line="-980"/>
        <source>Edit global settings and game folders</source>
        <translation>编辑全局设置和游戏文件夹</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Edit global input mapping</source>
        <translation>编辑全局输入映射</translation>
    </message>
    <message>
        <location line="+354"/>
        <source>Game folders</source>
        <translation>游戏文件夹</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add at least one game folder in global settings.</source>
        <translation>请先在全局设置中添加至少一个游戏文件夹。</translation>
    </message>
    <message>
        <location line="+254"/>
        <source>Clear this game&apos;s config and use global settings?</source>
        <translation>清除此游戏的配置并改用全局设置吗？</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+3"/>
        <source>Global settings</source>
        <translation>全局设置</translation>
    </message>
    <message>
        <location line="+29"/>
        <location line="+61"/>
        <location line="+3"/>
        <source>Import game configs</source>
        <translation>导入游戏配置</translation>
    </message>
    <message>
        <location line="-63"/>
        <location line="+105"/>
        <source>JSON files (*.json)</source>
        <translation>JSON 文件 (*.json)</translation>
    </message>
    <message>
        <location line="-99"/>
        <location line="+6"/>
        <location line="+11"/>
        <location line="+20"/>
        <source>Import failed</source>
        <translation>导入失败</translation>
    </message>
    <message>
        <location line="-30"/>
        <source>Expected a JSON object containing game configs keyed by PPSA code.</source>
        <translation>应为以 PPSA 代码为键、包含游戏配置的 JSON 对象。</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+20"/>
        <source>Invalid game config for %1. %2</source>
        <translation>%1 的游戏配置无效。%2</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>No matching games found in the library.</source>
        <translation>游戏库中没有找到匹配的游戏。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Configs will be updated for the following games:
</source>
        <translation>将更新以下游戏的配置：
</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>
%1 — %2</source>
        <translation>
%1 — %2</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>
Games not found: %1</source>
        <translation>
未找到游戏：%1</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>%1 — %2 (%3)</source>
        <translation>%1 — %2（%3）</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+5"/>
        <location line="+8"/>
        <source>Export game config</source>
        <translation>导出游戏配置</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>No game configs with a PPSA code were found.</source>
        <translation>没有找到带 PPSA 代码的游戏配置。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Game:</source>
        <translation>游戏：</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Export failed</source>
        <translation>导出失败</translation>
    </message>
    <message>
        <location line="+33"/>
        <location line="+6"/>
        <location line="+145"/>
        <source>Open game folder</source>
        <translation>打开游戏文件夹</translation>
    </message>
    <message>
        <location line="-151"/>
        <source>Game folder does not exist.</source>
        <translation>游戏文件夹不存在。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Could not open game folder.</source>
        <translation>无法打开游戏文件夹。</translation>
    </message>
    <message>
        <location line="+46"/>
        <location line="+11"/>
        <location line="+13"/>
        <source>Remove save data</source>
        <translation>删除存档数据</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>No save data folder found for this game.</source>
        <translation>没有找到此游戏的存档文件夹。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Remove save data for &quot;%1&quot;?

This will delete:
%2

This cannot be undone.</source>
        <translation>删除“%1”的存档数据吗？

将删除：
%2

此操作无法撤销。</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Could not remove:
%1</source>
        <translation>无法删除：
%1</translation>
    </message>
    <message>
        <location line="+72"/>
        <source>Run</source>
        <translation>运行</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>View trophies...</source>
        <translation>查看奖杯…</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Cheats (experimental)...</source>
        <translation>作弊功能（实验性）…</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Remove save data...</source>
        <translation>删除存档数据…</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Edit game config...</source>
        <translation>编辑游戏配置…</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Global settings...</source>
        <translation>全局设置…</translation>
    </message>
</context>
<context>
    <name>InputCaptureDialog</name>
    <message>
        <location filename="../src/inputMappingDialog.cpp" line="+138"/>
        <source>Set Binding</source>
        <translation>设置按键</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Press a key or mouse button.
F1, F7, and F11 are reserved; Esc cancels.</source>
        <translation>请按下键盘按键或鼠标按钮。
F1、F7 和 F11 已被占用；按 Esc 取消。</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>That key is reserved by the emulator.</source>
        <translation>该按键已被模拟器占用。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>That key is not supported.</source>
        <translation>不支持该按键。</translation>
    </message>
</context>
<context>
    <name>InputMappingDialog</name>
    <message>
        <location line="-138"/>
        <source>D-pad Up</source>
        <translation>方向键上</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>D-pad Down</source>
        <translation>方向键下</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>D-pad Left</source>
        <translation>方向键左</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>D-pad Right</source>
        <translation>方向键右</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Left stick Up</source>
        <translation>左摇杆上</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Left stick Down</source>
        <translation>左摇杆下</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Left stick Left</source>
        <translation>左摇杆左</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Left stick Right</source>
        <translation>左摇杆右</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Right stick Up</source>
        <translation>右摇杆上</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Right stick Down</source>
        <translation>右摇杆下</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Right stick Left</source>
        <translation>右摇杆左</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Right stick Right</source>
        <translation>右摇杆右</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Triangle</source>
        <translation>三角键</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Circle</source>
        <translation>圆圈键</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cross</source>
        <translation>叉键</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Square</source>
        <translation>方块键</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Options</source>
        <translation>选项键</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Touch pad left (SELECT)</source>
        <translation>触摸板左侧（选择）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Touch pad right (START)</source>
        <translation>触摸板右侧（开始）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Speaker volume (cycle)</source>
        <translation>扬声器音量（循环切换）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Vibration intensity (cycle)</source>
        <translation>振动强度（循环切换）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Trigger effect intensity (cycle)</source>
        <translation>扳机效果强度（循环切换）</translation>
    </message>
    <message>
        <location line="+163"/>
        <source>Input Mapping</source>
        <translation>输入映射</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Map keyboard or mouse buttons to DualSense controls.
Press F7 in-game to toggle mouse movement on the right stick.</source>
        <translation>将键盘或鼠标按钮映射到 DualSense 控件。
游戏中按 F7 可切换由鼠标移动控制右摇杆。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Mouse sensitivity</source>
        <translation>鼠标灵敏度</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>DualSense control</source>
        <translation>DualSense 控件</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Host input</source>
        <translation>主机输入</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Change...</source>
        <translation>更改…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Clear</source>
        <translation>清除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Defaults</source>
        <translation>默认值</translation>
    </message>
    <message>
        <location line="+81"/>
        <source>None</source>
        <translation>无</translation>
    </message>
</context>
<context>
    <name>MainDialog</name>
    <message>
        <location filename="../forms/main_dialog.ui" line="+20"/>
        <source>KytyPS5 GT7 - VR &amp; Render | masterzhang233</source>
        <translation>KytyPS5 GT7 - VR 与渲染 | masterzhang233</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>GT7  /  VR &amp; Render</source>
        <translation>GT7  /  VR 与渲染</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>masterzhang233 · based on KytyPS5-GT7</source>
        <translation>masterzhang233 · 基于 KytyPS5-GT7</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Select GT7 to configure internal 3D resolution or launch SteamVR.</source>
        <translation>选择 GT7 后可设置内部 3D 分辨率或启动 SteamVR。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Start in SteamVR</source>
        <translation>使用 SteamVR 启动</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Selects VR mode and starts the selected GT7 game after version and patch verification. Connect your headset in SteamVR first.</source>
        <translation>选择 VR 模式，并在完成版本和补丁验证后启动所选 GT7。请先在 SteamVR 中连接头显。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>VR &amp;&amp; render settings</source>
        <translation>VR 与渲染设置</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Internal 3D size and window output are separate.</source>
        <translation>内部 3D 尺寸与窗口输出相互独立。</translation>
    </message>
    <message>
        <location line="+38"/>
        <location line="+13"/>
        <location line="+9"/>
        <source>TextLabel</source>
        <translation>文本</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>&lt;a href=&quot;check&quot;&gt;Check for updates&lt;/a&gt;</source>
        <translation>&lt;a href=&quot;check&quot;&gt;检查更新&lt;/a&gt;</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Check for updates on startup</source>
        <translation>启动时检查更新</translation>
    </message>
    <message>
        <location filename="../src/mainDialog.cpp" line="+578"/>
        <source>GT7 profile conflict</source>
        <translation>GT7 配置冲突</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Disable the custom game patches before using GT7 internal rendering or SteamVR. These profiles require their own verified patch plan.</source>
        <translation>使用 GT7 内部渲染或 SteamVR 前，请禁用自定义游戏补丁。这些配置需要使用各自经过验证的补丁方案。</translation>
    </message>
    <message>
        <location line="+12"/>
        <location line="+82"/>
        <location line="+77"/>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <location line="-159"/>
        <source>Invalid emulator configuration</source>
        <translation>模拟器配置无效</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Checking GT7 version and patch compatibility...</source>
        <translation>正在检查 GT7 版本和补丁兼容性…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+49"/>
        <location line="+94"/>
        <source>

Log folder:
%1</source>
        <translation>

日志文件夹：
%1</translation>
    </message>
    <message>
        <location line="-91"/>
        <source>GT7 compatibility check failed</source>
        <translation>GT7 兼容性检查失败</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Can&apos;t create file:
</source>
        <translation>无法创建文件：
</translation>
    </message>
    <message>
        <location line="+70"/>
        <source>Failed to start:
%1

%2</source>
        <translation>启动失败：
%1

%2</translation>
    </message>
</context>
<context>
    <name>MainDialogPrivate</name>
    <message>
        <location line="-522"/>
        <source>Diagnostic logs: %1</source>
        <translation>诊断日志：%1</translation>
    </message>
    <message>
        <location line="-42"/>
        <location line="+48"/>
        <source>Open log folder</source>
        <translation>打开日志文件夹</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy latest log path</source>
        <translation>复制最新日志路径</translation>
    </message>
    <message>
        <location line="-47"/>
        <location line="+48"/>
        <source>Export diagnostics...</source>
        <translation>导出诊断包...</translation>
    </message>
    <message>
        <location line="-70"/>
        <source>The diagnostic package was saved and its path was copied to the clipboard:
%1

Nothing was uploaded. Known user, game and emulator paths were removed. Review the remaining text before sharing it.</source>
        <translation>诊断包已保存，其路径也已复制到剪贴板：
%1

没有上传任何文件。已移除已知的用户、游戏和模拟器路径，请在分享前检查其余文本。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>GT7 ended unexpectedly</source>
        <translation>GT7 异常结束</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>GT7 ended unexpectedly.

Exit code: %1 (0x%2)
Log folder:
%3

Open this run&apos;s log folder or export a diagnostic ZIP for analysis.</source>
        <translation>GT7 异常结束。

退出代码：%1 (0x%2)
日志文件夹：
%3

请打开本次运行的日志文件夹，或导出诊断 ZIP 以供分析。</translation>
    </message>
    <message>
        <location line="+59"/>
        <source>Open the folder containing one timestamped directory for each GT7 run.</source>
        <translation>打开日志文件夹，其中每次 GT7 运行都有一个按时间命名的目录。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy the latest GT7 run log directory to the clipboard.</source>
        <translation>将最新一次 GT7 运行日志目录复制到剪贴板。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Create a local ZIP with logs and a run summary. No file is uploaded automatically.</source>
        <translation>在本地创建包含日志和运行摘要的 ZIP 文件。程序不会自动上传任何文件。</translation>
    </message>
    <message>
        <location line="-51"/>
        <location line="+61"/>
        <location line="+574"/>
        <location filename="../tests/LauncherLanguageTests.cpp" line="+52"/>
        <source>Diagnostic logs</source>
        <translation>诊断日志</translation>
    </message>
    <message>
        <location line="-634"/>
        <location line="+61"/>
        <source>Could not open the log folder:
%1</source>
        <translation>无法打开日志文件夹：
%1</translation>
    </message>
    <message>
        <location line="-114"/>
        <location line="+10"/>
        <location line="+10"/>
        <source>Export diagnostics</source>
        <translation>导出诊断包</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>No GT7 run log is available yet.</source>
        <translation>目前还没有可用的 GT7 运行日志。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>ZIP archive (*.zip)</source>
        <translation>ZIP 压缩包 (*.zip)</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Diagnostics exported</source>
        <translation>诊断包已导出</translation>
    </message>
    <message>
        <location line="+146"/>
        <location line="+49"/>
        <source>Settings file: </source>
        <translation>设置文件：</translation>
    </message>
    <message>
        <location line="-29"/>
        <source>Emulator: </source>
        <translation>模拟器：</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Version: </source>
        <translation>版本：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Can&apos;t find emulator</source>
        <translation>找不到模拟器</translation>
    </message>
    <message>
        <location line="+478"/>
        <source>The game can still be started, but this run could not create its diagnostic folder:
%1</source>
        <translation>游戏仍可启动，但本次运行无法创建诊断文件夹：
%1</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>Native</source>
        <translation>原生</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1p nominal</source>
        <translation>标称 %1p</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>SteamVR · internal eye scale %1% · desktop output %2</source>
        <translation>SteamVR · 单眼内部缩放 %1% · 桌面输出 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Desktop · internal 3D %1 · window output %2 · VR preset %3%</source>
        <translation>桌面 · 内部 3D %1 · 窗口输出 %2 · VR 预设 %3%</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Select GT7 to configure internal 3D resolution or launch SteamVR.</source>
        <translation>选择 GT7 后可设置内部 3D 分辨率或启动 SteamVR。</translation>
    </message>
</context>
<context>
    <name>PatchesDialog</name>
    <message>
        <location filename="../src/patchesDialog.cpp" line="+22"/>
        <source>Cheats (experimental) - %1</source>
        <translation>作弊功能（实验性）- %1</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Apply selection</source>
        <translation>应用选择</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>No local cheat file: %1</source>
        <translation>没有本地作弊文件：%1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Invalid cheat JSON in %1.</source>
        <translation>%1 中的作弊 JSON 无效。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Cheat JSON has no &quot;mods&quot; array in %1.</source>
        <translation>%1 的作弊 JSON 中没有“mods”数组。</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+10"/>
        <source>Invalid cheat entry in %1.</source>
        <translation>%1 中的作弊条目无效。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Loaded %1 cheat(s) from %2.</source>
        <translation>已从 %2 加载 %1 个作弊项。</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Cheat file changed; reload the dialog.</source>
        <translation>作弊文件已更改，请重新打开此对话框。</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Could not save cheat file: %1</source>
        <translation>无法保存作弊文件：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Cheat selection saved.</source>
        <translation>作弊选项已保存。</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../src/configurationEditDialog.cpp" line="-576"/>
        <source>Custom</source>
        <translation>自定义</translation>
    </message>
    <message>
        <location filename="../src/trophyViewerDialog.cpp" line="+44"/>
        <source>Platinum</source>
        <translation>白金</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Gold</source>
        <translation>金</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Silver</source>
        <translation>银</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bronze</source>
        <translation>铜</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Unlocked</source>
        <translation>已解锁</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Trophy</source>
        <translation>奖杯</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Description</source>
        <translation>说明</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Grade: %1</source>
        <translation>等级：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hidden trophy</source>
        <translation>隐藏奖杯</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Reward: %1</source>
        <translation>奖励：%1</translation>
    </message>
</context>
<context>
    <name>TrophyViewerDialog</name>
    <message>
        <location line="+8"/>
        <location line="+30"/>
        <source>Trophy Viewer</source>
        <translation>奖杯查看器</translation>
    </message>
    <message>
        <location line="-5"/>
        <source>Trophy Viewer - %1</source>
        <translation>奖杯查看器 - %1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>No trophy package found in %1.</source>
        <translation>在 %1 中未找到奖杯包。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Could not read trophy package %1.</source>
        <translation>无法读取奖杯包 %1。</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>UNLOCKED</source>
        <translation>已解锁</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>LOCKED</source>
        <translation>未解锁</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Hidden Trophy</source>
        <translation>隐藏奖杯</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+13"/>
        <source>Trophy %1</source>
        <translation>奖杯 %1</translation>
    </message>
</context>
<context>
    <name>UpdateChecker</name>
    <message>
        <location filename="../src/updateChecker.cpp" line="+59"/>
        <source>Invalid update feed</source>
        <translation>更新源无效</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Incomplete update feed</source>
        <translation>更新源不完整</translation>
    </message>
    <message>
        <location line="+72"/>
        <location line="+9"/>
        <source>Update Check</source>
        <translation>检查更新</translation>
    </message>
    <message>
        <location line="-8"/>
        <source>Could not check for updates:
%1</source>
        <translation>无法检查更新：
%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>You are using the latest version (%1).</source>
        <translation>当前已是最新版本（%1）。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>A new build is available.

Current: %1
Latest: %2

Open the release page to download it?</source>
        <translation>发现新版本。

当前版本：%1
最新版本：%2

是否打开发布页面下载？</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>KytyPS5 Update</source>
        <translation>KytyPS5 更新</translation>
    </message>
</context>
</TS>
