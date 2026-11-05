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
    <message>
        <source>GT7 graphics preset</source>
        <translation>GT7 画面预设</translation>
    </message>
    <message>
        <source>Use the DLSS checkbox on the launcher: 1920 x 1080 to 2560 x 1440, with FSR 2x frame generation. Requires an NVIDIA RTX graphics card.</source>
        <translation>在启动器中勾选 DLSS 即可使用 1920 × 1080 → 2560 × 1440 超分辨率与 FSR 2× 帧生成。需要 NVIDIA RTX 显卡。</translation>
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
    <message>
        <location filename="../forms/configuration_list_widget.ui" line="128"/>
        <location filename="../src/configurationListWidget.cpp" line="218"/>
        <source>Map a racing wheel</source>
        <translation>设置方向盘映射</translation>
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
    <message>
        <source>Tohi-GT7 1.0.0</source>
        <translation>Tohi-GT7 1.0.0</translation>
    </message>
    <message>
        <source>Tohi-GT7</source>
        <translation>Tohi-GT7</translation>
    </message>
    <message>
        <source>1.0.0  /  RELEASE</source>
        <translation>1.0.0  /  正式版</translation>
    </message>
    <message>
        <source>Ready for your next drive.</source>
        <translation>下一程，即刻出发。</translation>
    </message>
    <message>
        <source>DLSS</source>
        <translation>DLSS</translation>
    </message>
    <message>
        <source>Fixed 1080p to 1440p DLSS with FSR 2x frame generation and wheel ghosting correction. Requires NVIDIA RTX. Changes apply on the next launch.</source>
        <translation>固定 1080p → 1440p DLSS，搭配 FSR 2× 帧生成与车轮拖影修正。需要 NVIDIA RTX 显卡，下次启动游戏时生效。</translation>
    </message>
    <message>
        <source>1920 x 1080 → 2560 x 1440 · DLSS + FSR 2x frame generation</source>
        <translation>1920 × 1080 → 2560 × 1440 · DLSS + FSR 2× 帧生成</translation>
    </message>
    <message>
        <source>Start game</source>
        <translation>启动游戏</translation>
    </message>
    <message>
        <source>GAME LIBRARY</source>
        <translation>游戏库</translation>
    </message>
    <message>
        <source>Disable the custom game patches before using the GT7 graphics preset. These profiles require their own verified patch plan.</source>
        <translation>使用 GT7 画面预设前，请禁用自定义游戏补丁。这些预设需要使用各自经过验证的补丁方案。</translation>
    </message>
    <message>
        <source>The DLSS preset needs the complete Tohi-GT7 release package.
Missing files: %1

Extract the full package, or turn off DLSS.</source>
        <translation>DLSS 预设需要完整的 Tohi-GT7 发布包。
缺少文件：%1

请完整解压发布包，或关闭 DLSS。</translation>
    </message>
    <message>
        <source>Graphics runtime missing</source>
        <translation>缺少图形运行库</translation>
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
    <message>
        <source>Start game</source>
        <translation>启动游戏</translation>
    </message>
    <message>
        <source>Running...</source>
        <translation>游戏运行中…</translation>
    </message>
    <message>
        <source>1920 x 1080 → 2560 x 1440 · DLSS + FSR 2x frame generation</source>
        <translation>1920 × 1080 → 2560 × 1440 · DLSS + FSR 2× 帧生成</translation>
    </message>
    <message>
        <source>DLSS and frame generation off · desktop rendering</source>
        <translation>DLSS 与帧生成已关闭 · 桌面渲染</translation>
    </message>
    <message>
        <source>Select Gran Turismo 7 in the game library to start.</source>
        <translation>请在游戏库中选择 Gran Turismo 7 后启动。</translation>
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
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="43"/>
        <source>Steering</source>
        <translation>转向</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="44"/>
        <source>Throttle</source>
        <translation>油门</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="45"/>
        <source>Brake</source>
        <translation>刹车</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="46"/>
        <source>Clutch</source>
        <translation>离合器</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="47"/>
        <source>Handbrake</source>
        <translation>手刹</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="48"/>
        <source>Cross ✕</source>
        <translation>叉号 ✕</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="49"/>
        <source>Circle ○</source>
        <translation>圆圈 ○</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="50"/>
        <source>Square □</source>
        <translation>方块 □</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="51"/>
        <source>Triangle △</source>
        <translation>三角 △</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="52"/>
        <source>D-pad up</source>
        <translation>方向键上</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="53"/>
        <source>D-pad down</source>
        <translation>方向键下</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="54"/>
        <source>D-pad left</source>
        <translation>方向键左</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="55"/>
        <source>D-pad right</source>
        <translation>方向键右</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="56"/>
        <source>L1 (left paddle)</source>
        <translation>L1（左拨片）</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="57"/>
        <source>R1 (right paddle)</source>
        <translation>R1（右拨片）</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="58"/>
        <source>L2</source>
        <translation>L2</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="59"/>
        <source>R2</source>
        <translation>R2</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="60"/>
        <source>L3</source>
        <translation>L3</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="61"/>
        <source>R3</source>
        <translation>R3</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="62"/>
        <source>Options</source>
        <translation>选项</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="63"/>
        <source>Touchpad press</source>
        <translation>触摸板按下</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="64"/>
        <source>Gear 1</source>
        <translation>1 挡</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="65"/>
        <source>Gear 2</source>
        <translation>2 挡</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="66"/>
        <source>Gear 3</source>
        <translation>3 挡</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="67"/>
        <source>Gear 4</source>
        <translation>4 挡</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="68"/>
        <source>Gear 5</source>
        <translation>5 挡</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="69"/>
        <source>Gear 6</source>
        <translation>6 挡</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="70"/>
        <source>Gear 7</source>
        <translation>7 挡</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="71"/>
        <source>Gear R</source>
        <translation>倒挡</translation>
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
    <message>
        <source>Tohi-GT7 Update</source>
        <translation>Tohi-GT7 更新</translation>
    </message>
</context>
<context>
    <name>WheelMappingDialog</name>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="97"/>
        <location filename="../src/wheelMappingDialog.cpp" line="104"/>
        <location filename="../src/wheelMappingDialog.cpp" line="255"/>
        <location filename="../src/wheelMappingDialog.cpp" line="355"/>
        <location filename="../src/wheelMappingDialog.cpp" line="709"/>
        <location filename="../src/wheelMappingDialog.cpp" line="715"/>
        <location filename="../src/wheelMappingDialog.cpp" line="739"/>
        <location filename="../src/wheelMappingDialog.cpp" line="751"/>
        <source>Racing wheel</source>
        <translation>方向盘</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="105"/>
        <source>%1 could not be read (%2); starting from an empty mapping.</source>
        <translation>无法读取 %1（%2）；将从空白映射开始。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="118"/>
        <source>SDL joystick support could not start: %1</source>
        <translation>无法启动 SDL 摇杆支持：%1</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="142"/>
        <source>Off</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="143"/>
        <source>Fanatec Gran Turismo DD Pro</source>
        <translation>Fanatec Gran Turismo DD Pro</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="148"/>
        <source>Emulated wheel</source>
        <translation>模拟方向盘</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="154"/>
        <source>Map the wheel here, not in the controller mapping. Pedals at rest should read 0 %; tick Reverse on a pedal that reads 100 % at rest.</source>
        <translation>请在此设置方向盘映射。踏板松开时的读数应为 0%；若松开时显示 100%，请勾选该踏板的“反向”。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="161"/>
        <source>Control</source>
        <translation>控制项</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="161"/>
        <source>Bound to</source>
        <translation>绑定输入</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="161"/>
        <source>Value</source>
        <translation>当前值</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="174"/>
        <source>Map</source>
        <translation>映射</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="175"/>
        <source>Clear</source>
        <translation>清除</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="176"/>
        <source>Reverse</source>
        <translation>反向</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="181"/>
        <source>Steering: around the centre, in percent of half the travel. Pedals: at the released end, in percent of the whole travel.</source>
        <translation>转向：中心附近的死区，以半程转向范围的百分比表示。踏板：松开端的死区，以全行程的百分比表示。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="186"/>
        <source>1 is linear; above 1 is finer near the centre or the released end, below 1 is quicker.</source>
        <translation>1 为线性响应；大于 1 时，中心或松开端附近的控制更精细；小于 1 时，响应更灵敏。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="191"/>
        <source>Dead zone</source>
        <translation>死区</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="193"/>
        <source>Curve</source>
        <translation>响应曲线</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="198"/>
        <source>Force feedback and rotation</source>
        <translation>力反馈与转角</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="206"/>
        <source>Strength</source>
        <translation>强度</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="208"/>
        <source>Force-feedback device</source>
        <translation>力反馈设备</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="210"/>
        <source>Steering axis (default)</source>
        <translation>转向轴（默认）</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="212"/>
        <source>Cartesian</source>
        <translation>笛卡尔坐标</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="213"/>
        <source>Polar</source>
        <translation>极坐标</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="214"/>
        <source>Change only if the wheel feels no force with the default; some DirectInput drivers ignore the steering-axis encoding.</source>
        <translation>仅在默认设置下方向盘没有力反馈时更改；部分 DirectInput 驱动不支持转向轴编码。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="216"/>
        <source>Direction encoding</source>
        <translation>方向编码</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="217"/>
        <source>Reverse force direction</source>
        <translation>反转力反馈方向</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="218"/>
        <source>Mirrors the game's forces. Run the force test first; tick this only if the wheel still pulls away from the centre in the game.</source>
        <translation>反转游戏输出的力反馈。请先运行力反馈测试；仅在游戏中方向盘仍向偏离中心的方向拉扯时勾选。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="223"/>
        <source> degrees</source>
        <translation> 度</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="224"/>
        <source>The rotation set in the wheel's own software. When the game sets a car's steering lock, steering is scaled to it.</source>
        <translation>填写方向盘配套软件中设置的转角。游戏指定车辆的最大转向角时，将据此缩放转向输入。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="226"/>
        <source>Wheel rotation</source>
        <translation>方向盘转角</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="228"/>
        <source>Force direction</source>
        <translation>力反馈方向</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="229"/>
        <source>Soft end stop at the car's steering lock</source>
        <translation>在车辆最大转向角处施加软限位</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="230"/>
        <source>Pushes back past the car's lock. Works once the force test has measured the wheel's force direction.</source>
        <translation>超过车辆最大转向角时施加回推力。力反馈测试测得方向盘的力反馈方向后生效。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="234"/>
        <source>Keep reading controllers when the game window has no focus (all of them, pads too)</source>
        <translation>游戏窗口失去焦点时继续读取所有控制器（包括手柄）</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="237"/>
        <source>Compute damper and friction in the emulator (for drivers that mishandle them)</source>
        <translation>由模拟器计算阻尼和摩擦力（适用于驱动处理异常的设备）</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="239"/>
        <source>Works once the force test has measured the wheel's force direction.</source>
        <translation>力反馈测试测得方向盘的力反馈方向后生效。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="249"/>
        <source>Import RPCS3 G27 mapping...</source>
        <translation>导入 RPCS3 G27 映射…</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="250"/>
        <location filename="../src/wheelMappingDialog.cpp" line="743"/>
        <source>Test force feedback</source>
        <translation>测试力反馈</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="256"/>
        <source>Steering is not mapped, so the game will not see the wheel. Save anyway?</source>
        <translation>尚未映射转向，游戏将无法识别方向盘。仍要保存吗？</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="323"/>
        <source>not measured: run Test force feedback</source>
        <translation>尚未测量：请运行“测试力反馈”</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="325"/>
        <source>measured</source>
        <translation>已测量</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="326"/>
        <source>measured (reversed)</source>
        <translation>已测量（反向）</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="356"/>
        <source>Cannot save %1: %2</source>
        <translation>无法保存 %1：%2</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="416"/>
        <source>The steering device</source>
        <translation>转向输入设备</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="428"/>
        <source>Not connected (0x%1)</source>
        <translation>未连接（0x%1）</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="437"/>
        <source>No joystick found. Connect and power the wheel.</source>
        <translation>未检测到摇杆设备。请连接方向盘并接通电源。</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/wheelMappingDialog.cpp" line="438"/>
        <source>%n device(s) connected. Select a control and press Map.</source>
        <translation>
            <numerusform>已连接 %n 个设备。请选择控制项并点击“映射”。</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="474"/>
        <source>(not bound)</source>
        <translation>（未绑定）</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="478"/>
        <source>absent %1:%2</source>
        <translation>未连接 %1:%2</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="482"/>
        <source>%1: axis %2</source>
        <translation>%1：轴 %2</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="483"/>
        <source>%1: button %2</source>
        <translation>%1：按钮 %2</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="485"/>
        <source>%1: hat %2 %3</source>
        <translation>%1：方向帽 %2 %3</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="501"/>
        <source>absent</source>
        <translation>未连接</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="538"/>
        <source>pressed</source>
        <translation>已按下</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="601"/>
        <source>%1 bound to %2.</source>
        <translation>%1 已绑定至 %2。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="604"/>
        <source>Nothing was pressed or moved; %1 unchanged.</source>
        <translation>未检测到按键或移动；%1 保持不变。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="637"/>
        <source>%1: %2 now (%3 s left)</source>
        <translation>%1：现在请%2（剩余 %3 秒）</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="639"/>
        <source>move the axis or press a button</source>
        <translation>移动轴或按下按钮</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="640"/>
        <source>press the button</source>
        <translation>按下按钮</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="702"/>
        <source>RPCS3 LogitechG27.yml</source>
        <translation>RPCS3 LogitechG27.yml</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="703"/>
        <source>RPCS3 G27 mapping (LogitechG27.yml);;YAML (*.yml)</source>
        <translation>RPCS3 G27 映射 (LogitechG27.yml);;YAML (*.yml)</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="716"/>
        <source>Not imported: %1</source>
        <translation>导入失败：%1</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="730"/>
        <source>Imported %1.</source>
        <translation>已导入 %1。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="740"/>
        <source>No force-feedback device is connected.</source>
        <translation>未连接力反馈设备。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="744"/>
        <source>The wheel will turn briefly one way and then the other, at a low force. Keep your hands near the rim, not on it.</source>
        <translation>方向盘将以较小的力向两个方向分别短暂转动。请将双手放在轮缘附近，不要握住方向盘。</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="752"/>
        <source>Cannot open force feedback: %1</source>
        <translation>无法启动力反馈：%1</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="788"/>
        <source>the device refused the effect: %1</source>
        <translation>设备拒绝了力反馈效果：%1</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="798"/>
        <source>%1 force: %2</source>
        <translation>%1力反馈：%2</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="799"/>
        <source>positive</source>
        <translation>正向</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="799"/>
        <source>negative</source>
        <translation>负向</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="800"/>
        <source>no movement</source>
        <translation>未转动</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="801"/>
        <source>turned right</source>
        <translation>向右转动</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="802"/>
        <source>turned left</source>
        <translation>向左转动</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="811"/>
        <source>force direction measured</source>
        <translation>已测得力反馈方向</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="814"/>
        <source>force direction measured (reversed)</source>
        <translation>已测得力反馈方向（反向）</translation>
    </message>
    <message>
        <location filename="../src/wheelMappingDialog.cpp" line="816"/>
        <source>unclear, not changed (hands off the rim, then test again)</source>
        <translation>方向不明确，未更改（请松开方向盘后重新测试）</translation>
    </message>
</context>
</TS>
