#import <Cocoa/Cocoa.h>
#import <unistd.h>

@interface FoloStatusValue : NSObject
@property(nonatomic, strong) NSTextField *dot;
@property(nonatomic, strong) NSTextField *text;
- (void)update:(NSString *)value color:(NSColor *)color;
@end

@implementation FoloStatusValue
- (instancetype)init {
    self = [super init];
    if (self) {
        _dot = [NSTextField labelWithString:@"●"];
        _dot.font = [NSFont systemFontOfSize:12 weight:NSFontWeightBold];
        _dot.textColor = NSColor.secondaryLabelColor;
        _text = [NSTextField labelWithString: @"等待检测"];
        _text.textColor = NSColor.secondaryLabelColor;
        _text.lineBreakMode = NSLineBreakByTruncatingMiddle;
        _text.maximumNumberOfLines = 1;
    }
    return self;
}

- (void)update:(NSString *)value color:(NSColor *)color {
    self.dot.textColor = color;
    self.text.stringValue = value ?: @"";
    self.text.textColor = [color isEqual:NSColor.systemRedColor]
        ? NSColor.systemRedColor : NSColor.labelColor;
    self.text.toolTip = value;
}
@end

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) FoloStatusValue *codexStatus;
@property(nonatomic, strong) FoloStatusValue *serviceStatus;
@property(nonatomic, strong) FoloStatusValue *wifiStatus;
@property(nonatomic, strong) FoloStatusValue *deviceStatus;
@property(nonatomic, strong) NSTextView *outputView;
@property(nonatomic, strong) NSProgressIndicator *progress;
@property(nonatomic, strong) NSButton *installButton;
@property(nonatomic, strong) NSButton *wifiButton;
@property(nonatomic, strong) NSButton *refreshButton;
@property(nonatomic, strong) NSButton *firmwareButton;
@property(nonatomic, strong) NSButton *pomodoroChoice;
@property(nonatomic, strong) NSButton *wordBearChoice;
@property(nonatomic, strong) NSTextField *firmwareSummary;
@property(nonatomic, strong) NSDictionary *firmwareCatalog;
@property(nonatomic, strong) NSButton *firmwareConfirmButton;
@property(nonatomic) BOOL firmwareInstalling;
@end

@implementation AppDelegate

- (instancetype)init {
    self = [super init];
    if (self) {
        _codexStatus = [FoloStatusValue new];
        _serviceStatus = [FoloStatusValue new];
        _wifiStatus = [FoloStatusValue new];
        _deviceStatus = [FoloStatusValue new];
    }
    return self;
}

- (NSURL *)resourcesURL {
    return NSBundle.mainBundle.resourceURL ?: [NSURL fileURLWithPath:NSFileManager.defaultManager.currentDirectoryPath];
}

- (NSURL *)toolsURL {
    return [[self resourcesURL] URLByAppendingPathComponent:@"tools" isDirectory:YES];
}

- (NSString *)serviceLabel {
    return @"com.folotoy.foloos-bridge";
}

- (NSString *)serviceName {
    return [NSString stringWithFormat:@"gui/%d/%@", getuid(), [self serviceLabel]];
}

- (NSURL *)plistURL {
    return [NSFileManager.defaultManager.homeDirectoryForCurrentUser
        URLByAppendingPathComponent:[NSString stringWithFormat:@"Library/LaunchAgents/%@.plist", [self serviceLabel]]];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    [self buildWindow];
    [NSApp activateIgnoringOtherApps:YES];
    [self.window makeKeyAndOrderFront:nil];
    [self refreshStatus:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    return self.firmwareInstalling ? NSTerminateCancel : NSTerminateNow;
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
    return !self.firmwareInstalling;
}

- (NSArray<NSView *> *)statusRow:(NSString *)title value:(FoloStatusValue *)value {
    NSTextField *name = [NSTextField labelWithString:title];
    name.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
    name.textColor = NSColor.secondaryLabelColor;
    NSStackView *valueStack = [NSStackView stackViewWithViews:@[value.dot, value.text]];
    valueStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    valueStack.spacing = 7;
    return @[name, valueStack];
}

- (void)buildWindow {
    self.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 650, 675)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered
        defer:NO];
    self.window.title = @"FoloOS 编程伴侣";
    [self.window center];
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;

    NSTextField *title = [NSTextField labelWithString:@"FoloOS 编程伴侣"];
    title.font = [NSFont systemFontOfSize:26 weight:NSFontWeightBold];
    NSTextField *subtitle = [NSTextField labelWithString:@"一次安装，以后随 Mac 登录自动连接"];
    subtitle.font = [NSFont systemFontOfSize:14];
    subtitle.textColor = NSColor.secondaryLabelColor;
    NSStackView *header = [NSStackView stackViewWithViews:@[title, subtitle]];
    header.orientation = NSUserInterfaceLayoutOrientationVertical;
    header.alignment = NSLayoutAttributeLeading;
    header.spacing = 5;

    NSTextField *statusTitle = [NSTextField labelWithString:@"当前状态"];
    statusTitle.font = [NSFont systemFontOfSize:16 weight:NSFontWeightSemibold];
    NSGridView *statusGrid = [NSGridView gridViewWithViews:@[
        [self statusRow:@"可用 Codex CLI" value:self.codexStatus],
        [self statusRow:@"后台桥接" value:self.serviceStatus],
        [self statusRow:@"设备 Wi-Fi" value:self.wifiStatus],
        [self statusRow:@"设备连接" value:self.deviceStatus],
    ]];
    statusGrid.rowSpacing = 10;
    statusGrid.columnSpacing = 14;
    [statusGrid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    [statusGrid columnAtIndex:1].xPlacement = NSGridCellPlacementLeading;

    NSBox *statusBox = [NSBox new];
    statusBox.boxType = NSBoxCustom;
    statusBox.cornerRadius = 10;
    statusBox.borderWidth = 1;
    statusBox.borderColor = NSColor.separatorColor;
    statusBox.fillColor = NSColor.controlBackgroundColor;
    NSStackView *statusStack = [NSStackView stackViewWithViews:@[statusTitle, statusGrid]];
    statusStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    statusStack.alignment = NSLayoutAttributeLeading;
    statusStack.spacing = 12;
    statusStack.translatesAutoresizingMaskIntoConstraints = NO;
    [statusBox.contentView addSubview:statusStack];
    [NSLayoutConstraint activateConstraints:@[
        [statusStack.leadingAnchor constraintEqualToAnchor:statusBox.contentView.leadingAnchor constant:18],
        [statusStack.trailingAnchor constraintEqualToAnchor:statusBox.contentView.trailingAnchor constant:-18],
        [statusStack.topAnchor constraintEqualToAnchor:statusBox.contentView.topAnchor constant:16],
        [statusStack.bottomAnchor constraintEqualToAnchor:statusBox.contentView.bottomAnchor constant:-16],
    ]];

    self.installButton = [NSButton buttonWithTitle:@"安装 / 修复桥接" target:self action:@selector(installBridge:)];
    self.installButton.bezelStyle = NSBezelStyleRounded;
    self.installButton.controlSize = NSControlSizeLarge;
    self.installButton.keyEquivalent = @"\r";
    self.wifiButton = [NSButton buttonWithTitle:@"配置设备 Wi-Fi…" target:self action:@selector(configureWiFi:)];
    self.wifiButton.bezelStyle = NSBezelStyleRounded;
    self.wifiButton.controlSize = NSControlSizeLarge;
    self.refreshButton = [NSButton buttonWithTitle:@"重新检测" target:self action:@selector(refreshStatus:)];
    self.refreshButton.bezelStyle = NSBezelStyleRounded;
    self.refreshButton.controlSize = NSControlSizeLarge;
    NSButton *logsButton = [NSButton buttonWithTitle:@"打开日志" target:self action:@selector(openLogs:)];
    logsButton.bezelStyle = NSBezelStyleRounded;
    logsButton.controlSize = NSControlSizeLarge;
    NSStackView *buttons = [NSStackView stackViewWithViews:@[self.installButton, self.wifiButton, self.refreshButton, logsButton]];
    buttons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    buttons.spacing = 10;
    buttons.distribution = NSStackViewDistributionFillEqually;

    self.firmwareButton = [NSButton buttonWithTitle:@"自定义安装应用…" target:self action:@selector(customizeFirmware:)];
    self.firmwareButton.bezelStyle = NSBezelStyleRounded;
    NSTextField *firmwareNote = [NSTextField labelWithString:@"选择离线应用，通过已配对的 Wi-Fi 安装到设备"];
    firmwareNote.font = [NSFont systemFontOfSize:12];
    firmwareNote.textColor = NSColor.secondaryLabelColor;
    NSStackView *firmwareRow = [NSStackView stackViewWithViews:@[self.firmwareButton, firmwareNote]];
    firmwareRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    firmwareRow.spacing = 10;

    self.progress = [NSProgressIndicator new];
    self.progress.style = NSProgressIndicatorStyleSpinning;
    self.progress.controlSize = NSControlSizeSmall;
    self.progress.displayedWhenStopped = NO;
    NSTextField *outputTitle = [NSTextField labelWithString:@"详细信息"];
    outputTitle.font = [NSFont systemFontOfSize:14 weight:NSFontWeightSemibold];
    NSStackView *outputHeader = [NSStackView stackViewWithViews:@[outputTitle, self.progress]];
    outputHeader.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    outputHeader.spacing = 8;

    self.outputView = [NSTextView new];
    self.outputView.editable = NO;
    self.outputView.selectable = YES;
    self.outputView.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    self.outputView.textColor = NSColor.labelColor;
    self.outputView.backgroundColor = NSColor.textBackgroundColor;
    self.outputView.textContainerInset = NSMakeSize(10, 10);
    self.outputView.string = @"正在检测…";
    NSScrollView *scroll = [NSScrollView new];
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;
    scroll.documentView = self.outputView;
    [[scroll.heightAnchor constraintGreaterThanOrEqualToConstant:150] setActive:YES];

    NSTextField *note = [NSTextField labelWithString:@"桥接安装与 USB 配网已拆分。没插设备时也可以先安装桥接。"];
    note.textColor = NSColor.secondaryLabelColor;
    note.font = [NSFont systemFontOfSize:12];
    note.maximumNumberOfLines = 2;

    NSStackView *root = [NSStackView stackViewWithViews:@[header, statusBox, buttons, firmwareRow, outputHeader, scroll, note]];
    root.orientation = NSUserInterfaceLayoutOrientationVertical;
    root.alignment = NSLayoutAttributeLeading;
    root.spacing = 16;
    root.translatesAutoresizingMaskIntoConstraints = NO;
    [self.window.contentView addSubview:root];
    [NSLayoutConstraint activateConstraints:@[
        [root.leadingAnchor constraintEqualToAnchor:self.window.contentView.leadingAnchor constant:28],
        [root.trailingAnchor constraintEqualToAnchor:self.window.contentView.trailingAnchor constant:-28],
        [root.topAnchor constraintEqualToAnchor:self.window.contentView.topAnchor constant:24],
        [root.bottomAnchor constraintEqualToAnchor:self.window.contentView.bottomAnchor constant:-22],
        [statusBox.widthAnchor constraintEqualToAnchor:root.widthAnchor],
        [buttons.widthAnchor constraintEqualToAnchor:root.widthAnchor],
        [outputHeader.widthAnchor constraintEqualToAnchor:root.widthAnchor],
        [scroll.widthAnchor constraintEqualToAnchor:root.widthAnchor],
        [note.widthAnchor constraintEqualToAnchor:root.widthAnchor],
    ]];
}

- (NSString *)pythonPath {
    NSArray<NSString *> *candidates = @[@"/usr/bin/python3", @"/opt/homebrew/bin/python3", @"/usr/local/bin/python3"];
    for (NSString *candidate in candidates) {
        if ([NSFileManager.defaultManager isExecutableFileAtPath:candidate]) {
            return candidate;
        }
    }
    return nil;
}

- (NSDictionary *)runCommand:(NSString *)executable
                   arguments:(NSArray<NSString *> *)arguments
                       input:(NSData *)input {
    NSTask *task = [NSTask new];
    task.launchPath = executable;
    task.arguments = arguments;
    task.environment = NSProcessInfo.processInfo.environment;
    NSPipe *outputPipe = [NSPipe pipe];
    task.standardOutput = outputPipe;
    task.standardError = outputPipe;
    NSPipe *inputPipe = nil;
    if (input) {
        inputPipe = [NSPipe pipe];
        task.standardInput = inputPipe;
    }
    @try {
        [task launch];
        if (input) {
            [inputPipe.fileHandleForWriting writeData:input];
            [inputPipe.fileHandleForWriting closeFile];
        }
        NSData *data = [outputPipe.fileHandleForReading readDataToEndOfFile];
        [task waitUntilExit];
        NSString *output = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] ?: @"";
        return @{@"code": @(task.terminationStatus), @"output": output};
    } @catch (NSException *exception) {
        return @{@"code": @127, @"output": exception.reason ?: @"未知错误"};
    }
}

- (NSDictionary *)runCommand:(NSString *)executable arguments:(NSArray<NSString *> *)arguments {
    return [self runCommand:executable arguments:arguments input:nil];
}

- (void)setBusy:(BOOL)busy message:(NSString *)message {
    self.installButton.enabled = !busy;
    self.wifiButton.enabled = !busy;
    self.refreshButton.enabled = !busy;
    self.firmwareButton.enabled = !busy;
    if (busy) {
        [self.progress startAnimation:nil];
    } else {
        [self.progress stopAnimation:nil];
    }
    if (message) {
        self.outputView.string = message;
    }
}

- (void)display:(NSString *)text {
    self.outputView.string = [text ?: @"" stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    [self.outputView scrollToBeginningOfDocument:nil];
}

- (NSDictionary *)findCodexUsingPython:(NSString *)python {
    return [self runCommand:python arguments:@[[[[self toolsURL] URLByAppendingPathComponent:@"install_autostart.py"] path], @"--find-codex"]];
}

- (NSDictionary *)readWiFiConfig {
    NSURL *url = [NSFileManager.defaultManager.homeDirectoryForCurrentUser
        URLByAppendingPathComponent:@"Library/Application Support/FoloOS/bridge.json"];
    NSData *data = [NSData dataWithContentsOfURL:url];
    if (!data) {
        return @{@"text": @"未配置", @"color": NSColor.systemOrangeColor};
    }
    NSDictionary *json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    NSString *host = [json[@"host"] isKindOfClass:NSString.class] ? json[@"host"] : @"";
    if (host.length == 0) {
        return @{@"text": @"已保存，等待设备上线", @"color": NSColor.systemOrangeColor};
    }
    return @{@"text": host, @"color": NSColor.systemGreenColor};
}

- (NSString *)combinedLog {
    NSURL *directory = [NSFileManager.defaultManager.homeDirectoryForCurrentUser
        URLByAppendingPathComponent:@"Library/Logs/FoloOS" isDirectory:YES];
    NSMutableArray<NSString *> *parts = [NSMutableArray array];
    for (NSString *name in @[@"bridge.log", @"bridge-error.log"]) {
        NSData *data = [NSData dataWithContentsOfURL:[directory URLByAppendingPathComponent:name]];
        if (!data) continue;
        if (data.length > 131072) {
            data = [data subdataWithRange:NSMakeRange(data.length - 131072, 131072)];
        }
        NSString *text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        if (text) [parts addObject:text];
    }
    return [parts componentsJoinedByString:@"\n"];
}

- (NSDictionary *)readDeviceStatus {
    NSString *log = [self combinedLog];
    NSRange connected = [log rangeOfString:@"设备已通过 Wi-Fi 连接" options:NSBackwardsSearch];
    NSRange disconnected = [log rangeOfString:@"无线连接中断" options:NSBackwardsSearch];
    if (connected.location != NSNotFound &&
        (disconnected.location == NSNotFound || connected.location > disconnected.location)) {
        return @{@"text": @"已通过 Wi-Fi 认证", @"color": NSColor.systemGreenColor};
    }
    if (disconnected.location != NSNotFound) {
        return @{@"text": @"暂未连接，后台会自动重连", @"color": NSColor.systemOrangeColor};
    }
    return @{@"text": @"暂无连接记录", @"color": NSColor.secondaryLabelColor};
}

- (NSString *)lastLogLines {
    NSArray<NSString *> *all = [[self combinedLog] componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet];
    NSMutableArray<NSString *> *nonempty = [NSMutableArray array];
    for (NSString *line in all) {
        if ([line stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet].length) {
            [nonempty addObject:line];
        }
    }
    NSUInteger start = nonempty.count > 12 ? nonempty.count - 12 : 0;
    return [[nonempty subarrayWithRange:NSMakeRange(start, nonempty.count - start)] componentsJoinedByString:@"\n"];
}

- (void)refreshStatus:(id)sender {
    [self setBusy:YES message:@"正在检测 Codex、后台桥接和设备连接…"];
    [self.codexStatus update:@"检测中…" color:NSColor.secondaryLabelColor];
    [self.serviceStatus update:@"检测中…" color:NSColor.secondaryLabelColor];
    [self.wifiStatus update:@"检测中…" color:NSColor.secondaryLabelColor];
    [self.deviceStatus update:@"检测中…" color:NSColor.secondaryLabelColor];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString *python = [self pythonPath];
        NSDictionary *codex = python ? [self findCodexUsingPython:python] : nil;
        NSDictionary *service = [self runCommand:@"/bin/launchctl" arguments:@[@"print", [self serviceName]]];
        NSDictionary *wifi = [self readWiFiConfig];
        NSDictionary *device = [self readDeviceStatus];
        NSMutableArray<NSString *> *details = [NSMutableArray array];
        [details addObject:python ? [NSString stringWithFormat:@"Python：%@", python] : @"Python：未找到（需安装 Xcode Command Line Tools 或 Python 3）"];
        if (codex) [details addObject:[NSString stringWithFormat:@"Codex：%@", codex[@"output"]]];
        NSString *log = [self lastLogLines];
        if (log.length) [details addObject:[NSString stringWithFormat:@"\n最近日志：\n%@", log]];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!python) {
                [self.codexStatus update:@"运行环境不完整" color:NSColor.systemRedColor];
            } else if ([codex[@"code"] intValue] == 0) {
                NSArray<NSString *> *lines = [codex[@"output"] componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet];
                NSString *path = @"已找到";
                for (NSString *line in lines.reverseObjectEnumerator) {
                    if (line.length) { path = line; break; }
                }
                [self.codexStatus update:path color:NSColor.systemGreenColor];
            } else {
                [self.codexStatus update:@"未找到可启动 app-server 的 CLI" color:NSColor.systemRedColor];
            }
            BOOL running = [service[@"code"] intValue] == 0 && [service[@"output"] containsString:@"state = running"];
            [self.serviceStatus update:(running ? @"已安装并运行" : @"未运行") color:(running ? NSColor.systemGreenColor : NSColor.systemOrangeColor)];
            [self.wifiStatus update:wifi[@"text"] color:wifi[@"color"]];
            [self.deviceStatus update:device[@"text"] color:device[@"color"]];
            [self display:[details componentsJoinedByString:@"\n"]];
            [self setBusy:NO message:nil];
        });
    });
}

- (void)installBridge:(id)sender {
    NSString *python = [self pythonPath];
    if (!python) {
        [self showAlert:@"缺少 Python 3 运行环境" message:@"请先安装 Xcode Command Line Tools 或 Python 3。完成后再点击安装。"];
        return;
    }
    [self setBusy:YES message:@"正在核对 Codex CLI 和登录状态…"];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSDictionary *codex = [self findCodexUsingPython:python];
        if ([codex[@"code"] intValue] != 0) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self display:codex[@"output"]];
                [self setBusy:NO message:nil];
                [self showCodexHelp];
            });
            return;
        }
        NSString *codexPath = @"";
        for (NSString *line in [codex[@"output"] componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet].reverseObjectEnumerator) {
            if (line.length) { codexPath = line; break; }
        }
        NSDictionary *login = [self runCommand:codexPath arguments:@[@"login", @"status"]];
        if ([login[@"code"] intValue] != 0) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self display:login[@"output"]];
                [self setBusy:NO message:nil];
                [self showAlert:@"Codex 还没有登录" message:@"请先打开 Codex 完成登录，然后回到这里重新安装。"];
            });
            return;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [self display:@"正在安装稳定运行文件并注册开机自启…"];
        });
        NSDictionary *result = [self runCommand:python arguments:@[[[[self toolsURL] URLByAppendingPathComponent:@"install_autostart.py"] path]]];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self display:result[@"output"]];
            [self setBusy:NO message:nil];
            if ([result[@"code"] intValue] == 0) {
                [self showAlert:@"安装完成" message:@"桥接已启动，以后登录 Mac 会自动运行。设备 Wi-Fi 可以另外配置。"];
                [self refreshStatus:nil];
            } else {
                [self showAlert:@"安装失败" message:result[@"output"]];
            }
        });
    });
}

- (NSString *)firmwareDirectory {
    return [[self.resourcesURL URLByAppendingPathComponent:@"firmware" isDirectory:YES] path];
}

- (NSArray<NSString *> *)selectedOfflineApps {
    NSMutableArray<NSString *> *apps = [NSMutableArray array];
    if (self.pomodoroChoice.state == NSControlStateValueOn) [apps addObject:@"pomodoro"];
    if (self.wordBearChoice.state == NSControlStateValueOn) [apps addObject:@"word_bear"];
    return apps;
}

- (void)updateFirmwareSummary:(id)sender {
    NSSet *selected = [NSSet setWithArray:self.selectedOfflineApps];
    NSDictionary *match = nil;
    NSUInteger fullSize = 0;
    for (NSDictionary *variant in self.firmwareCatalog[@"variants"]) {
        if ([variant[@"apps"] count] == 2) fullSize = [variant[@"size_bytes"] unsignedIntegerValue];
        if ([[NSSet setWithArray:variant[@"apps"]] isEqualToSet:selected]) match = variant;
    }
    self.firmwareConfirmButton.enabled = match != nil;
    if (!match) {
        self.firmwareSummary.stringValue = @"安装包不包含此组合，请重新获取完整安装包。";
        return;
    }
    NSUInteger size = [match[@"size_bytes"] unsignedIntegerValue];
    NSUInteger capacity = [self.firmwareCatalog[@"slot_bytes"] unsignedIntegerValue];
    self.firmwareSummary.stringValue = [NSString stringWithFormat:
        @"固件 %.1f KiB / %.0f KiB\n剩余 %.1f KiB · 比完整版本节省 %.1f KiB",
        size / 1024.0, capacity / 1024.0, (capacity - size) / 1024.0,
        fullSize > size ? (fullSize - size) / 1024.0 : 0.0];
}

- (void)customizeFirmware:(id)sender {
    NSString *python = self.pythonPath;
    if (!python) {
        [self showAlert:@"缺少 Python 3 运行环境" message:@"无法运行固件安装程序。"];
        return;
    }
    [self setBusy:YES message:@"正在校验安装包内的固件和容量信息…"];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSDictionary *result = [self runCommand:python arguments:@[
            [[self.toolsURL URLByAppendingPathComponent:@"firmware_catalog.py"] path],
            @"--directory", self.firmwareDirectory, @"--catalog"
        ]];
        NSData *data = [result[@"output"] dataUsingEncoding:NSUTF8StringEncoding];
        id catalog = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self setBusy:NO message:nil];
            if ([result[@"code"] intValue] != 0 || ![catalog isKindOfClass:NSDictionary.class]) {
                [self display:result[@"output"]];
                [self showAlert:@"无法读取自定义固件" message:result[@"output"]];
                return;
            }
            self.firmwareCatalog = catalog;
            [self display:@"固件校验通过，可以选择要安装的离线应用。"];
            [self chooseFirmwareWithPython:python];
        });
    });
}

- (void)chooseFirmwareWithPython:(NSString *)python {
    self.pomodoroChoice = [NSButton checkboxWithTitle:@"番茄专注" target:self action:@selector(updateFirmwareSummary:)];
    self.wordBearChoice = [NSButton checkboxWithTitle:@"单词熊（词库与离线发音）" target:self action:@selector(updateFirmwareSummary:)];
    NSArray *saved = [NSUserDefaults.standardUserDefaults arrayForKey:@"offlineApps"];
    self.pomodoroChoice.state = (!saved || [saved containsObject:@"pomodoro"]) ? NSControlStateValueOn : NSControlStateValueOff;
    self.wordBearChoice.state = (!saved || [saved containsObject:@"word_bear"]) ? NSControlStateValueOn : NSControlStateValueOff;
    NSTextField *base = [NSTextField labelWithString:@"始终安装：编程伴侣、系统设置"];
    base.textColor = NSColor.secondaryLabelColor;
    self.firmwareSummary = [NSTextField labelWithString:@""];
    self.firmwareSummary.maximumNumberOfLines = 2;
    NSStackView *stack = [NSStackView stackViewWithViews:@[base, self.pomodoroChoice, self.wordBearChoice, self.firmwareSummary]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 12;
    stack.frame = NSMakeRect(0, 0, 410, 140);
    NSAlert *alert = [NSAlert new];
    alert.messageText = @"选择要安装的离线应用";
    alert.informativeText = @"将通过 Wi-Fi 更新整份固件并重启设备。未勾选的应用及专属资源不会写入；Wi-Fi 设置和已有学习进度保留。需要设备已配对并连接同一局域网。下方选项是本次安装选择。";
    alert.accessoryView = stack;
    self.firmwareConfirmButton = [alert addButtonWithTitle:@"安装到设备"];
    [alert addButtonWithTitle:@"取消"];
    [self updateFirmwareSummary:nil];
    if ([alert runModal] != NSAlertFirstButtonReturn) return;
    NSArray<NSString *> *apps = self.selectedOfflineApps;
    self.firmwareInstalling = YES;
    [self setBusy:YES message:@"正在暂停桥接并安装所选固件，请保持设备供电和 Wi-Fi 连接…"];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        BOOL wasRunning = [[self runCommand:@"/bin/launchctl" arguments:@[@"print", self.serviceName]][@"code"] intValue] == 0;
        if (wasRunning) [self stopService];
        NSMutableArray *arguments = [NSMutableArray arrayWithArray:@[
            @"-u", [[self.toolsURL URLByAppendingPathComponent:@"firmware_catalog.py"] path],
            @"--directory", self.firmwareDirectory, @"--install", @"--apps"
        ]];
        [arguments addObjectsFromArray:apps];
        NSDictionary *result = [self runFirmwareCommand:python arguments:arguments];
        if (wasRunning) [self startServiceIfInstalled];
        dispatch_async(dispatch_get_main_queue(), ^{
            self.firmwareInstalling = NO;
            [self setBusy:NO message:nil];
            [self display:result[@"output"]];
            if ([result[@"code"] intValue] == 0) {
                [NSUserDefaults.standardUserDefaults setObject:apps forKey:@"offlineApps"];
                [self showAlert:@"固件更新完成" message:@"设备将重启并显示所选应用。请在设备上确认菜单和应用可正常使用。"];
            } else {
                [self showAlert:@"固件安装未完成" message:result[@"output"]];
            }
        });
    });
}

- (NSDictionary *)runFirmwareCommand:(NSString *)python arguments:(NSArray<NSString *> *)arguments {
    NSTask *task = [NSTask new];
    task.executableURL = [NSURL fileURLWithPath:python];
    task.arguments = arguments;
    NSPipe *pipe = [NSPipe pipe];
    task.standardOutput = pipe;
    task.standardError = pipe;
    NSError *error = nil;
    if (![task launchAndReturnError:&error]) {
        return @{@"code": @127, @"output": error.localizedDescription ?: @"无法启动安装程序"};
    }
    NSMutableData *output = [NSMutableData data];
    for (;;) {
        NSData *chunk = pipe.fileHandleForReading.availableData;
        if (!chunk.length) break;
        [output appendData:chunk];
        NSString *text = [[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding];
        if (text) dispatch_async(dispatch_get_main_queue(), ^{
            self.outputView.string = text;
            [self.outputView scrollToEndOfDocument:nil];
        });
    }
    [task waitUntilExit];
    NSString *text = [[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding] ?: @"无法解码安装日志";
    return @{@"code": @(task.terminationStatus), @"output": text};
}

- (void)showCodexHelp {
    NSAlert *alert = [NSAlert new];
    alert.alertStyle = NSAlertStyleWarning;
    alert.messageText = @"没有找到可用的 Codex CLI";
    alert.informativeText = @"已发现的包装程序不一定能启动桥接需要的 app-server。请先完成 OpenAI 官方 Codex CLI 安装。";
    [alert addButtonWithTitle:@"打开官方安装说明"];
    [alert addButtonWithTitle:@"取消"];
    if ([alert runModal] == NSAlertFirstButtonReturn) {
        [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"https://learn.chatgpt.com/docs/codex/cli"]];
    }
}

- (NSArray<NSString *> *)usbPorts {
    NSArray<NSString *> *names = [NSFileManager.defaultManager contentsOfDirectoryAtPath:@"/dev" error:nil] ?: @[];
    NSMutableArray<NSString *> *ports = [NSMutableArray array];
    for (NSString *name in names) {
        if ([name hasPrefix:@"cu.usbmodem"]) [ports addObject:[@"/dev/" stringByAppendingString:name]];
    }
    [ports sortUsingSelector:@selector(compare:)];
    return ports;
}

- (void)configureWiFi:(id)sender {
    NSString *python = [self pythonPath];
    if (!python) {
        [self showAlert:@"缺少 Python 3 运行环境" message:@"无法运行 USB 配网程序。"];
        return;
    }
    NSArray<NSString *> *ports = [self usbPorts];
    if (!ports.count) {
        [self showAlert:@"没有找到 AI Passport USB 串口" message:@"请插好支持数据的 USB 线，并先让 Chrome 刷机页断开设备。"];
        return;
    }
    NSTextField *ssid = [NSTextField new];
    ssid.placeholderString = @"2.4 GHz Wi-Fi 名称";
    NSSecureTextField *password = [NSSecureTextField new];
    password.placeholderString = @"Wi-Fi 密码（开放网络留空）";
    NSPopUpButton *port = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [port addItemsWithTitles:ports];
    NSStackView *stack = [NSStackView stackViewWithViews:@[ssid, password, port]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.spacing = 8;
    stack.frame = NSMakeRect(0, 0, 390, 86);
    [[ssid.widthAnchor constraintEqualToConstant:390] setActive:YES];
    NSAlert *alert = [NSAlert new];
    alert.messageText = @"配置设备 Wi-Fi";
    alert.informativeText = @"该操作只配网，不会重新安装或覆盖桥接。";
    alert.accessoryView = stack;
    [alert addButtonWithTitle:@"开始配网"];
    [alert addButtonWithTitle:@"取消"];
    if ([alert runModal] != NSAlertFirstButtonReturn) return;
    NSString *networkName = [ssid.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (!networkName.length) {
        [self showAlert:@"Wi-Fi 名称不能为空" message:@"请重新配置。"];
        return;
    }
    NSString *selectedPort = port.titleOfSelectedItem ?: ports.firstObject;
    NSData *passwordData = [[password.stringValue stringByAppendingString:@"\n"] dataUsingEncoding:NSUTF8StringEncoding];
    [self setBusy:YES message:@"正在暂停后台桥接并向设备写入 Wi-Fi…"];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        [self stopService];
        NSDictionary *result = [self runCommand:python arguments:@[
            [[self.toolsURL URLByAppendingPathComponent:@"mac_bridge.py"] path],
            @"--setup-wifi", @"--port", selectedPort, @"--ssid", networkName, @"--password-stdin"
        ] input:passwordData];
        [self startServiceIfInstalled];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self display:result[@"output"]];
            [self setBusy:NO message:nil];
            if ([result[@"code"] intValue] == 0) {
                [self showAlert:@"配网成功" message:[NSString stringWithFormat:@"设备已连接 %@。现在可以拔掉 USB。", networkName]];
                [self refreshStatus:nil];
            } else {
                [self showAlert:@"配网失败" message:result[@"output"]];
            }
        });
    });
}

- (void)stopService {
    if (![NSFileManager.defaultManager fileExistsAtPath:self.plistURL.path]) return;
    [self runCommand:@"/bin/launchctl" arguments:@[@"bootout", [NSString stringWithFormat:@"gui/%d", getuid()], self.plistURL.path]];
}

- (void)startServiceIfInstalled {
    if (![NSFileManager.defaultManager fileExistsAtPath:self.plistURL.path]) return;
    [self runCommand:@"/bin/launchctl" arguments:@[@"bootstrap", [NSString stringWithFormat:@"gui/%d", getuid()], self.plistURL.path]];
    [self runCommand:@"/bin/launchctl" arguments:@[@"kickstart", @"-k", self.serviceName]];
}

- (void)openLogs:(id)sender {
    NSURL *directory = [NSFileManager.defaultManager.homeDirectoryForCurrentUser
        URLByAppendingPathComponent:@"Library/Logs/FoloOS" isDirectory:YES];
    [NSFileManager.defaultManager createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:nil error:nil];
    [NSWorkspace.sharedWorkspace openURL:directory];
}

- (void)showAlert:(NSString *)title message:(NSString *)message {
    NSAlert *alert = [NSAlert new];
    alert.messageText = title;
    alert.informativeText = [message ?: @"" stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    [alert addButtonWithTitle:@"好"];
    [alert runModal];
}
@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *application = NSApplication.sharedApplication;
        AppDelegate *delegate = [AppDelegate new];
        application.delegate = delegate;
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        [application run];
    }
    return 0;
}
