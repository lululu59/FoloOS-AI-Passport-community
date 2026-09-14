#include <assert.h>
#include <string.h>

#include "coding_flow.h"

static void test_voice_requires_review(void)
{
    coding_flow_t flow;
    coding_flow_init(&flow);
    assert(flow.stage == CODING_STAGE_OFFLINE);
    assert(coding_flow_begin_recording(&flow) == CODING_ACTION_NONE);

    coding_flow_set_connected(&flow, true);
    assert(flow.stage == CODING_STAGE_IDLE);
    assert(coding_flow_begin_recording(&flow) == CODING_ACTION_START_CAPTURE);
    assert(flow.stage == CODING_STAGE_RECORDING);
    assert(coding_flow_end_recording(&flow) ==
           CODING_ACTION_STOP_CAPTURE);
    assert(flow.stage == CODING_STAGE_TRANSCRIBING);
    assert(coding_flow_confirm_voice(&flow) == CODING_ACTION_NONE);
    assert(coding_flow_set_transcript(&flow, "Fix the failing build"));
    assert(flow.stage == CODING_STAGE_VOICE_REVIEW);
    assert(coding_flow_confirm_voice(&flow) == CODING_ACTION_SEND_VOICE);
    assert(flow.stage == CODING_STAGE_QUEUED);
    assert(coding_flow_cancel_task(&flow) == CODING_ACTION_CANCEL_TASK);
    assert(flow.stage == CODING_STAGE_IDLE);
}

static void test_voice_retry_and_cancel(void)
{
    coding_flow_t flow;
    coding_flow_init(&flow);
    coding_flow_set_connected(&flow, true);
    coding_flow_begin_recording(&flow);
    coding_flow_end_recording(&flow);
    assert(coding_flow_set_transcript(&flow, "Wrong transcript"));
    coding_flow_retry_voice(&flow);
    assert(flow.stage == CODING_STAGE_IDLE);
    assert(flow.transcript[0] == '\0');

    coding_flow_begin_recording(&flow);
    coding_flow_end_recording(&flow);
    coding_flow_cancel_voice(&flow);
    assert(flow.stage == CODING_STAGE_IDLE);
}

static void test_long_chinese_transcript_is_not_cut_at_old_limit(void)
{
    coding_flow_t flow;
    const char *text =
        "现在可以接收了，然后我发现语音转文字有长度限制。我需要确认录音有没有完整收到，"
        "并且希望设备能够让我手动查看后面的每一页内容，而不是静默丢掉后半段文字。"
        "这一段特意写得超过旧版本的一百九十二字节缓存，用来验证修复确实生效。";

    coding_flow_init(&flow);
    coding_flow_set_connected(&flow, true);
    coding_flow_begin_recording(&flow);
    coding_flow_end_recording(&flow);
    assert(strlen(text) > 192U);
    assert(coding_flow_set_transcript(&flow, text));
    assert(strcmp(flow.transcript, text) == 0);
}

static void test_approval_requires_second_confirmation(void)
{
    coding_flow_t flow;
    coding_flow_init(&flow);
    coding_flow_set_connected(&flow, true);
    coding_flow_set_task_status(&flow, "Editing files");
    assert(coding_flow_show_approval(&flow, "Run npm install?", "Network and files"));
    assert(flow.stage == CODING_STAGE_APPROVAL_REVIEW);
    assert(coding_flow_confirm_approval(&flow) == CODING_ACTION_NONE);

    coding_flow_next_decision(&flow);
    assert(flow.decision == CODING_DECISION_APPROVE_SESSION);
    assert(coding_flow_open_approval_confirm(&flow));
    assert(flow.stage == CODING_STAGE_APPROVAL_CONFIRM);
    coding_flow_back_to_approval(&flow);
    assert(flow.stage == CODING_STAGE_APPROVAL_REVIEW);

    coding_flow_previous_decision(&flow);
    assert(flow.decision == CODING_DECISION_APPROVE_ONCE);
    assert(coding_flow_open_approval_confirm(&flow));
    assert(coding_flow_confirm_approval(&flow) == CODING_ACTION_SEND_APPROVAL);
    assert(flow.stage == CODING_STAGE_RUNNING);
}

static void test_disconnect_clears_sensitive_text(void)
{
    coding_flow_t flow;
    char long_text[CODING_DETAIL_MAX * 2];
    memset(long_text, 'x', sizeof(long_text) - 1U);
    long_text[sizeof(long_text) - 1U] = '\0';

    coding_flow_init(&flow);
    coding_flow_set_connected(&flow, true);
    assert(coding_flow_show_approval(&flow, "Approve?", long_text));
    assert(strlen(flow.approval_detail) == CODING_DETAIL_MAX - 1U);
    coding_flow_set_connected(&flow, false);
    assert(flow.stage == CODING_STAGE_OFFLINE);
    assert(flow.approval_question[0] == '\0');
    assert(flow.approval_detail[0] == '\0');
}

static void test_chinese_labels(void)
{
    assert(strcmp(coding_flow_stage_name(CODING_STAGE_APPROVAL_REVIEW),
                  "等待审批") == 0);
    assert(strcmp(coding_flow_decision_name(CODING_DECISION_APPROVE_ONCE),
                  "仅本次允许") == 0);
    assert(strcmp(coding_flow_decision_name(CODING_DECISION_CANCEL_TASK),
                  "取消任务") == 0);
    assert(strcmp(coding_flow_error_title("麦克风不可用"), "录音失败") == 0);
    assert(strcmp(coding_flow_error_title("识别不到文字"), "语音识别失败") == 0);
    assert(strcmp(coding_flow_error_title("bridge offline"), "连接中断") == 0);
    assert(strcmp(coding_flow_error_title("录音中连接断开"), "连接中断") == 0);
}

int main(void)
{
    test_voice_requires_review();
    test_voice_retry_and_cancel();
    test_long_chinese_transcript_is_not_cut_at_old_limit();
    test_approval_requires_second_confirmation();
    test_disconnect_clears_sensitive_text();
    test_chinese_labels();
    return 0;
}
