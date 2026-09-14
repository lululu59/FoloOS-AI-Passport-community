# 给 FoloOS 添加自己的小应用

这份源码可以修改、编译，并刷入 FoloToy AI Passport。应用和 FoloOS 一起编译；目前不支持把单独的应用包安装进已刷好的系统。

## 1. 获取源码

```sh
git clone https://github.com/lululu59/FoloOS-AI-Passport-community.git
cd FoloOS-AI-Passport-community
```

也可以在仓库页面点击 **Code → Download ZIP** 后解压，或者下载仓库里的 `FoloOS-source-20260914.zip`（仅源码，体积更小）。

## 2. 先编译原版

安装并激活 **ESP-IDF 5.5.3**，目标为 **ESP32-C3**。按 [ESP-IDF 官方安装说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/get-started/index.html) 准备环境。macOS / Linux 需要加载自己安装目录下的 `export.sh`；Windows 使用 ESP-IDF 提供的终端。`get_idf553` 是维护者本机的快捷命令，不是开发者需要具备的工具。

在源码根目录运行：

```sh
idf.py build
```

`sdkconfig.defaults` 已设置 `esp32c3`、8 MB Flash 和双 OTA 分区。首次构建会下载 `dependencies.lock` 中锁定的组件，包括 LVGL 9.5.0。生成的图片 C 数组、中文字库 C 文件和 WAV 均已包含，正常编译不需要重新生成素材，也不需要 API Key。

主要应用镜像为 `build/FoloToy-AI-Passport.bin`。它不是能从 `0x0` 直接写入的完整合并镜像。开发调试时使用 `idf.py -p <你的串口> flash`，由构建工具按分区和地址写入。此命令会更新设备上的固件，请先保留恢复方式。不要自动执行 `erase-flash`。

## 3. 新增 Hello 小应用

仓库提供 [examples/app_hello.c](examples/app_hello.c)。它显示一段文字，短按确认键返回主页；系统保留长按上键返回主页的行为。示例默认不加入固件，现有四个应用保持原样。

**第一步：复制文件。** 将 `examples/app_hello.c` 复制为 `main/app_hello.c`。

**第二步：声明接口。** 在 `main/app.h` 末尾加入：

```c
void app_hello_enter(void);
void app_hello_exit(void);
void app_hello_key(bsp_btn_t button, bsp_btn_ev_t event);
```

**第三步：注册菜单。** 在 `main/main.c` 的 `APPS[]` 中，现有“系统设置”这一行之后、数组结束之前追加：

```c
    { "Hello", &ui_icon_settings, app_hello_enter, app_hello_exit, app_hello_key },
```

这里暂时复用设置图标。**保留原有四项的顺序，在末尾追加**；现有代码通过固定索引识别编程伴侣和单词熊。菜单会自动计算应用数量并滚动显示，不要把 `VISIBLE_APP_COUNT` 改成应用总数。

**第四步：加入构建。** 在 `main/CMakeLists.txt` 的 `SRCS` 列表中加入 `"app_hello.c"`，位置可紧跟 `"app_settings.c"`。

**第五步：重新编译。**

```sh
idf.py build
```

检查构建结果后，再手动刷入自己的设备。主菜单应出现第五项 Hello。验证进入、确认键返回、长按上键返回，以及原有四个应用仍能正常进入。

## 4. 应用必须遵守的边界

- `enter()` 创建和显示页面；`exit()` 删除页面、停止定时器并释放本应用资源；`key()` 处理实体按键。
- 页面和按键入口由系统在 LVGL 锁内调用；自己的后台任务操作 LVGL 时需遵循 BSP 锁接口。按键回调不能阻塞等待网络、录音或长时间计算。
- 复用 `components/bsp` 的屏幕、按键、音频和 I2C，不要重复初始化共享总线或 ADC。
- 屏幕 240×320，无触摸。ESP32-C3 无 PSRAM，每个 OTA 槽为 3 MiB；关注构建容量和真机运行内存。
- 先使用英文应用名。新增中文菜单名称可能需要重新生成 20 px 子集字体；16 px 正文字库已包含 CJK Unified Ideographs 区段。
- 与本机 Codex 交互需要配置桥接；离线小应用不需要 Mac 桥接。

硬件依据见 [BSP 引脚定义](components/bsp/include/bsp_pins.h) 与 [硬件开发指南](docs/AI_HARDWARE_DEVELOPMENT_GUIDE.md)。后者包含上游 BSP 和历史分支说明，当前应用入口以 `main/main.c` 为准。

## 5. 字体与素材

预生成资源已经可以直接构建。需要扩充字体时，可安装 `lv_font_conv@1.5.3`，然后运行 `sh tools/generate_pixel_fonts.sh`。工具默认从 PATH 查找 `lv_font_conv`，也可通过 `LV_FONT_CONV` 指定可执行文件；字体默认使用 `third_party/fusion-pixel-font/` 中的 OTF。`FOLOOS_FONT` 可指定另一字体文件。字体遵循同目录的 OFL 许可。

两个可选图片转换脚本依赖 Pillow；它们用于把自己准备的素材转换为固件资源，固件构建不会调用它们。

## 6. 运行现有主机测试

macOS / Linux 的 C 逻辑检查：

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_ui_pixel_math.c main/ui_pixel_math.c -o /tmp/foloos-ui-test
/tmp/foloos-ui-test
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_pomodoro_math.c main/pomodoro_math.c -o /tmp/foloos-pomodoro-test
/tmp/foloos-pomodoro-test
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_coding_flow.c main/coding_flow.c -o /tmp/foloos-coding-test
/tmp/foloos-coding-test
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_word_bear_model.c main/word_bear_model.c main/word_bear_words.c -o /tmp/foloos-word-test
/tmp/foloos-word-test
```

macOS 上使用 Python 3.10+ 运行现有桥接测试（含 POSIX 伪终端测试，不可把整组直接当作 Windows 真机测试）：

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_*.py' -v
zsh tests/test_mac_installer_proxy.zsh
```

这些测试使用模拟音频和临时数据，不需要播放声音或刷设备。本次发布验证记录见 [docs/SOURCE_RELEASE_20260914.md](docs/SOURCE_RELEASE_20260914.md)。

## 7. 让 AI 帮你开发

可以将下载的仓库交给 AI，并附上：

> 请先阅读 AGENTS.md、DEVELOPMENT.md、main/app.h、main/main.c 和相关 BSP 接口。为 FoloOS 添加一个【填写功能】小应用，保留现有四个应用，在菜单末尾追加。使用 ESP-IDF 5.5.3，不增加动态插件系统。先编译，再分别报告主机测试结果和需要我在真机确认的内容；未经要求不要自动刷机。

修改后可以在自己的 Fork 中发布固件，或提交 Pull Request。不要提交 Wi-Fi 密码、设备配对文件、Codex 登录信息、录音和个人日志。
