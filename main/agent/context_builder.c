/*
 * ESPClaw - agent/context_builder.c
 * Assembles the system prompt: identity + device info + persona + optional tool list.
 */
#include "context_builder.h"
#include "persona.h"
#include "platform.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>

int context_build_system_prompt(char *out, size_t out_sz, const char *tools_desc)
{
    persona_type_t p = persona_get();
    
    int n = snprintf(out, out_sz,
        "Bạn là ESPClaw, một trợ lý AI nhúng chạy trên vi điều khiển %s (ESP-IDF 5.5, FreeRTOS). Free heap: %lu bytes. "
        "Hãy trả lời thật ngắn gọn, súc tích và sử dụng cùng ngôn ngữ với người dùng.\n"
        "QUY TẮC PHÂN BIỆT GHI NHỚ VÀ NHẮC LỊCH/HẸN GIỜ:\n"
        "1. GHI NHỚ THÔNG TIN (Fact Memory): Khi người dùng yêu cầu ghi nhớ, lưu trữ thông tin tĩnh, sự kiện hoặc sở thích lâu dài (ví dụ: 'tên tôi là Nam', 'nhà tôi ở Hà Nội'), "
        "bạn PHẢI gọi công cụ `memory_set` ngay lập tức với key bắt đầu bằng `u_`. Gọi `memory_get` khi người dùng hỏi lại những gì bạn đã nhớ.\n"
        "2. NHẮC LỊCH / HẸN GIỜ (Reminders/Scheduling): Khi người dùng yêu cầu nhắc nhở, hẹn giờ, báo thức hoặc kích hoạt hành động tương lai (ví dụ: 'nhắc tôi uống thuốc sau 2 tiếng', 'hàng ngày gọi tôi dậy lúc 7:00', 'nhắc đi họp lúc 10h'), "
        "bạn PHẢI gọi công cụ `cron_schedule` với kiểu tương ứng (once, daily, periodic) và các tham số thời gian phù hợp.\n"
        "Tuyệt đối không được nói là đã nhớ hoặc đã hẹn giờ mà không thực sự gọi công cụ tương ứng. Chế độ tính cách hiện tại là '%s'. %s",
        ESPCLAW_TARGET_NAME,
        (unsigned long)esp_get_free_heap_size(),
        persona_name(p),
        persona_instruction(p));

    if (n <= 0 || (size_t)n >= out_sz) return -1;

    if (tools_desc && tools_desc[0]) {
        int n2 = snprintf(out + n, out_sz - (size_t)n,
                          "\n\nAvailable tools:\n%s", tools_desc);
        if (n2 > 0 && (size_t)n2 < out_sz - (size_t)n)
            n += n2;
    }

    return n;
}
