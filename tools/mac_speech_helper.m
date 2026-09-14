#import <Foundation/Foundation.h>
#import <Speech/Speech.h>

static NSLock *s_lock;
static BOOL s_finished;
static SFSpeechRecognitionTask *s_task;

static void Finish(int code, NSString *output, NSString *error)
{
    [s_lock lock];
    if (s_finished) {
        [s_lock unlock];
        return;
    }
    s_finished = YES;
    [s_lock unlock];

    if (output.length > 0) {
        fprintf(stdout, "%s\n", output.UTF8String);
    }
    if (error.length > 0) {
        fprintf(stderr, "%s\n", error.UTF8String);
    }
    fflush(stdout);
    fflush(stderr);
    exit(code);
}

int main(int argc, const char *argv[])
{
    @autoreleasepool {
        s_lock = [[NSLock alloc] init];
        if (argc < 2) {
            Finish(2, nil, @"缺少 WAV 文件路径");
        }

        NSString *audioPath = [NSString stringWithUTF8String:argv[1]];
        NSString *localeID = argc >= 3
                                 ? [NSString stringWithUTF8String:argv[2]]
                                 : @"zh-CN";
        NSURL *audioURL = [NSURL fileURLWithPath:audioPath];

        [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
            if (status != SFSpeechRecognizerAuthorizationStatusAuthorized) {
                Finish(3, nil, @"macOS 未授权语音识别，请到系统设置 > 隐私与安全性 > 语音识别中允许 FoloOS");
                return;
            }

            SFSpeechRecognizer *recognizer =
                [[SFSpeechRecognizer alloc] initWithLocale:
                    [[NSLocale alloc] initWithLocaleIdentifier:localeID]];
            if (recognizer == nil || !recognizer.available) {
                Finish(4, nil, @"macOS 当前无法使用中文语音识别");
                return;
            }

            SFSpeechURLRecognitionRequest *request =
                [[SFSpeechURLRecognitionRequest alloc] initWithURL:audioURL];
            request.shouldReportPartialResults = NO;
            request.taskHint = SFSpeechRecognitionTaskHintDictation;
            s_task = [recognizer recognitionTaskWithRequest:request
                                              resultHandler:^(SFSpeechRecognitionResult *result,
                                                              NSError *recognitionError) {
                if (result != nil && result.final) {
                    NSString *text = [result.bestTranscription.formattedString
                        stringByTrimmingCharactersInSet:
                            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                    if (text.length == 0) {
                        Finish(5, nil, @"未识别到清晰文字，请靠近麦克风重试");
                    } else {
                        Finish(0, text, nil);
                    }
                } else if (recognitionError != nil) {
                    Finish(6, nil, [NSString stringWithFormat:@"语音识别失败：%@",
                                    recognitionError.localizedDescription]);
                }
            }];
        }];

        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC),
                       dispatch_get_main_queue(), ^{
            [s_task cancel];
            Finish(7, nil, @"语音识别超时，请检查网络或重试");
        });
        dispatch_main();
    }
    return 0;
}
