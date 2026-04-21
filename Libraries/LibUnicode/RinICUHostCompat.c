#include "../../../rinicu/rin_icu.h"

int rin_service_start(int scope, const char* id);
void rin_sleep(unsigned int ms);

typedef enum rin_host_handle_kind {
    RIN_HOST_HANDLE_NONE = 0,
    RIN_HOST_HANDLE_COLLATOR = 1,
    RIN_HOST_HANDLE_SEGMENTER = 2,
    RIN_HOST_HANDLE_NUMBER = 3,
    RIN_HOST_HANDLE_DATETIME = 4,
    RIN_HOST_HANDLE_PLURAL = 5
} rin_host_handle_kind_t;

typedef struct rin_host_handle {
    rin_host_handle_kind_t kind;
    size_t cursor;
    size_t text_len;
    uint32_t segment_kind;
} rin_host_handle_t;

static rin_host_handle_t g_handles[256];
static rin_icu_handle_t g_next_handle = 1;

static size_t c_strlen(char const* string)
{
    size_t length = 0;
    if (!string)
        return 0;
    while (string[length] != '\0')
        ++length;
    return length;
}

static void c_memcpy(void* dest, void const* src, size_t count)
{
    size_t i;
    unsigned char* destination = (unsigned char*)dest;
    unsigned char const* source = (unsigned char const*)src;
    for (i = 0; i < count; ++i)
        destination[i] = source[i];
}

static void c_memset(void* dest, int value, size_t count)
{
    size_t i;
    unsigned char* destination = (unsigned char*)dest;
    for (i = 0; i < count; ++i)
        destination[i] = (unsigned char)value;
}

static int c_strcmp(char const* lhs, char const* rhs)
{
    size_t index = 0;
    if (!lhs)
        lhs = "";
    if (!rhs)
        rhs = "";
    while (lhs[index] != '\0' && rhs[index] != '\0') {
        if (lhs[index] != rhs[index])
            return (unsigned char)lhs[index] - (unsigned char)rhs[index];
        ++index;
    }
    return (unsigned char)lhs[index] - (unsigned char)rhs[index];
}

static int format_signed_i64(long long value, char* dest, size_t dest_cap, size_t* out_len)
{
    char buffer[32];
    size_t length = 0;
    unsigned long long magnitude;

    if (!dest || dest_cap == 0)
        return -1;

    if (value < 0) {
        magnitude = (unsigned long long)(-(value + 1)) + 1;
    } else {
        magnitude = (unsigned long long)value;
    }

    do {
        buffer[length++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0 && length < sizeof(buffer));

    if (value < 0 && length < sizeof(buffer))
        buffer[length++] = '-';

    if (length >= dest_cap)
        length = dest_cap - 1;

    for (size_t i = 0; i < length; ++i)
        dest[i] = buffer[length - 1 - i];
    dest[length] = '\0';

    if (out_len)
        *out_len = length;
    return 0;
}

static int format_double_simple(double value, char* dest, size_t dest_cap, size_t* out_len)
{
    long long rounded = (long long)value;
    return format_signed_i64(rounded, dest, dest_cap, out_len);
}

static int copy_text(char const* source, char* dest, size_t dest_cap, size_t* out_len)
{
    size_t len;
    if (!dest || dest_cap == 0)
        return -1;
    if (!source)
        source = "";
    len = c_strlen(source);
    if (len >= dest_cap)
        len = dest_cap - 1;
    c_memcpy(dest, source, len);
    dest[len] = '\0';
    if (out_len)
        *out_len = len;
    return 0;
}

static rin_host_handle_t* get_handle(rin_icu_handle_t handle, rin_host_handle_kind_t expected_kind)
{
    if (handle == 0 || handle >= g_next_handle || handle >= (sizeof(g_handles) / sizeof(g_handles[0])))
        return NULL;
    if (g_handles[handle].kind != expected_kind)
        return NULL;
    return &g_handles[handle];
}

static int allocate_handle(rin_host_handle_kind_t kind, rin_icu_handle_t* out_handle)
{
    rin_icu_handle_t handle;
    if (!out_handle)
        return -1;
    handle = g_next_handle++;
    if (handle >= (sizeof(g_handles) / sizeof(g_handles[0])))
        return -1;
    c_memset(&g_handles[handle], 0, sizeof(g_handles[handle]));
    g_handles[handle].kind = kind;
    *out_handle = handle;
    return 0;
}

static int destroy_handle(rin_icu_handle_t handle, rin_host_handle_kind_t expected_kind)
{
    rin_host_handle_t* state = get_handle(handle, expected_kind);
    if (!state)
        return -1;
    c_memset(state, 0, sizeof(*state));
    return 0;
}

static int time_zone_offset_minutes(char const* time_zone)
{
    if (!time_zone)
        return 0;
    if (c_strcmp(time_zone, "Asia/Tokyo") == 0)
        return 9 * 60;
    if (c_strcmp(time_zone, "America/New_York") == 0)
        return -5 * 60;
    if (c_strcmp(time_zone, "Europe/Berlin") == 0)
        return 60;
    return 0;
}

static char const* relative_time_unit_name(uint32_t unit)
{
    switch (unit) {
    case RIN_ICU_TIME_UNIT_SECOND:
        return "second";
    case RIN_ICU_TIME_UNIT_MINUTE:
        return "minute";
    case RIN_ICU_TIME_UNIT_HOUR:
        return "hour";
    case RIN_ICU_TIME_UNIT_DAY:
        return "day";
    case RIN_ICU_TIME_UNIT_WEEK:
        return "week";
    case RIN_ICU_TIME_UNIT_MONTH:
        return "month";
    case RIN_ICU_TIME_UNIT_QUARTER:
        return "quarter";
    case RIN_ICU_TIME_UNIT_YEAR:
        return "year";
    default:
        return "unit";
    }
}

int rin_service_start(int scope, const char* id)
{
    (void)scope;
    (void)id;
    return 0;
}

void rin_sleep(unsigned int ms)
{
    (void)ms;
}

int rin_icu_client_open(rin_icu_client_t* out_client)
{
    if (!out_client)
        return -1;
    out_client->fd = 1;
    out_client->next_request_id = 1;
    out_client->reserved0 = 0;
    return 0;
}

void rin_icu_client_close(rin_icu_client_t* client)
{
    if (!client)
        return;
    client->fd = -1;
}

int rin_icu_locale_canonicalize(rin_icu_client_t* client, char const* input, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    return copy_text(input, dest, dest_cap, out_len);
}

int rin_icu_locale_resolve(rin_icu_client_t* client, char const* input, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    return copy_text(input, dest, dest_cap, out_len);
}

int rin_icu_normalize(rin_icu_client_t* client, int form, char const* input, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    (void)form;
    return copy_text(input, dest, dest_cap, out_len);
}

int rin_icu_locale_maximize(rin_icu_client_t* client, char const* input, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    return copy_text(input, dest, dest_cap, out_len);
}

int rin_icu_locale_minimize(rin_icu_client_t* client, char const* input, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    return copy_text(input, dest, dest_cap, out_len);
}

int rin_icu_locale_available(rin_icu_client_t* client, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    return copy_text("en,en-US,ja,ja-JP,de,de-DE,fr,fr-FR,zh,zh-CN", dest, dest_cap, out_len);
}

int rin_icu_locale_preferred(rin_icu_client_t* client, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    return copy_text("en-US", dest, dest_cap, out_len);
}

int rin_icu_collator_create(rin_icu_client_t* client, char const* locale, rin_icu_collator_options_t const* options, rin_icu_handle_t* out_handle)
{
    (void)client;
    (void)locale;
    (void)options;
    return allocate_handle(RIN_HOST_HANDLE_COLLATOR, out_handle);
}

int rin_icu_collator_compare(rin_icu_client_t* client, rin_icu_handle_t handle, char const* lhs, char const* rhs, int* out_result)
{
    int result = 0;
    (void)client;
    if (!get_handle(handle, RIN_HOST_HANDLE_COLLATOR) || !out_result)
        return -1;
    if (!lhs)
        lhs = "";
    if (!rhs)
        rhs = "";
    result = c_strcmp(lhs, rhs);
    *out_result = (result < 0) ? -1 : (result > 0 ? 1 : 0);
    return 0;
}

int rin_icu_collator_sort_key(rin_icu_client_t* client, rin_icu_handle_t handle, const char* input, uint8_t* dest, size_t dest_cap, size_t* out_len)
{
    size_t len;
    (void)client;
    if (!get_handle(handle, RIN_HOST_HANDLE_COLLATOR) || !dest || dest_cap == 0)
        return -1;
    if (!input)
        input = "";
    len = c_strlen(input);
    if (len >= dest_cap)
        len = dest_cap - 1;
    c_memcpy(dest, input, len);
    dest[len] = 0;
    if (out_len)
        *out_len = len + 1;
    return 0;
}

int rin_icu_collator_sort_keys_bulk(rin_icu_client_t* client, rin_icu_handle_t handle, const char* const* inputs, size_t input_count, uint8_t* dest, size_t dest_cap, size_t* out_len)
{
    size_t i;
    size_t cursor = 0;
    (void)client;
    if (!get_handle(handle, RIN_HOST_HANDLE_COLLATOR) || !dest)
        return -1;
    for (i = 0; i < input_count; ++i) {
        char const* input = inputs ? inputs[i] : "";
        size_t len = c_strlen(input);
        if (cursor + len + 1 > dest_cap)
            return -1;
        c_memcpy(dest + cursor, input, len);
        cursor += len;
        dest[cursor++] = 0;
    }
    if (out_len)
        *out_len = cursor;
    return 0;
}

int rin_icu_collator_destroy(rin_icu_client_t* client, rin_icu_handle_t handle)
{
    (void)client;
    return destroy_handle(handle, RIN_HOST_HANDLE_COLLATOR);
}

int rin_icu_segmenter_create(rin_icu_client_t* client, char const* locale, rin_icu_segmenter_options_t const* options, rin_icu_handle_t* out_handle)
{
    rin_host_handle_t* handle_state;
    (void)client;
    (void)locale;
    if (allocate_handle(RIN_HOST_HANDLE_SEGMENTER, out_handle) != 0)
        return -1;
    handle_state = get_handle(*out_handle, RIN_HOST_HANDLE_SEGMENTER);
    if (!handle_state)
        return -1;
    handle_state->segment_kind = options ? options->kind : RIN_ICU_SEGMENTATION_GRAPHEME;
    return 0;
}

int rin_icu_segmenter_reset(rin_icu_client_t* client, rin_icu_handle_t handle, char const* input)
{
    rin_host_handle_t* state;
    (void)client;
    state = get_handle(handle, RIN_HOST_HANDLE_SEGMENTER);
    if (!state)
        return -1;
    state->cursor = 0;
    state->text_len = c_strlen(input);
    return 0;
}

int rin_icu_segmenter_next(rin_icu_client_t* client, rin_icu_handle_t handle, rin_icu_segment_t* out_segment, int* out_has_value)
{
    rin_host_handle_t* state;
    unsigned char current_char;
    (void)client;
    if (!out_segment || !out_has_value)
        return -1;
    state = get_handle(handle, RIN_HOST_HANDLE_SEGMENTER);
    if (!state)
        return -1;
    if (state->cursor >= state->text_len) {
        c_memset(out_segment, 0, sizeof(*out_segment));
        *out_has_value = 0;
        return 0;
    }
    current_char = (unsigned char)'a';
    out_segment->start = (uint32_t)state->cursor;
    out_segment->end = (uint32_t)(state->cursor + 1);
    out_segment->flags = 0;
    if (state->segment_kind == RIN_ICU_SEGMENTATION_WORD && ((current_char >= '0' && current_char <= '9') || (current_char >= 'A' && current_char <= 'Z') || (current_char >= 'a' && current_char <= 'z')))
        out_segment->flags |= RIN_ICU_SEGMENT_FLAG_WORD_LIKE;
    state->cursor++;
    *out_has_value = 1;
    return 0;
}

int rin_icu_segmenter_destroy(rin_icu_client_t* client, rin_icu_handle_t handle)
{
    (void)client;
    return destroy_handle(handle, RIN_HOST_HANDLE_SEGMENTER);
}

int rin_icu_number_formatter_create(rin_icu_client_t* client, char const* locale, rin_icu_number_formatter_options_t const* options, rin_icu_handle_t* out_handle)
{
    (void)client;
    (void)locale;
    (void)options;
    return allocate_handle(RIN_HOST_HANDLE_NUMBER, out_handle);
}

int rin_icu_number_formatter_format(rin_icu_client_t* client, rin_icu_handle_t handle, double value, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    if (!get_handle(handle, RIN_HOST_HANDLE_NUMBER))
        return -1;
    return format_double_simple(value, dest, dest_cap, out_len);
}

int rin_icu_number_formatter_destroy(rin_icu_client_t* client, rin_icu_handle_t handle)
{
    (void)client;
    return destroy_handle(handle, RIN_HOST_HANDLE_NUMBER);
}

int rin_icu_datetime_formatter_create(rin_icu_client_t* client, char const* locale, rin_icu_datetime_formatter_options_t const* options, rin_icu_handle_t* out_handle)
{
    (void)client;
    (void)locale;
    (void)options;
    return allocate_handle(RIN_HOST_HANDLE_DATETIME, out_handle);
}

int rin_icu_datetime_formatter_format_epoch_ms(rin_icu_client_t* client, rin_icu_handle_t handle, int64_t epoch_ms, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    if (!get_handle(handle, RIN_HOST_HANDLE_DATETIME))
        return -1;
    return format_signed_i64((long long)epoch_ms, dest, dest_cap, out_len);
}

int rin_icu_datetime_formatter_destroy(rin_icu_client_t* client, rin_icu_handle_t handle)
{
    (void)client;
    return destroy_handle(handle, RIN_HOST_HANDLE_DATETIME);
}

int rin_icu_plural_rules_create(rin_icu_client_t* client, char const* locale, rin_icu_plural_rules_options_t const* options, rin_icu_handle_t* out_handle)
{
    (void)client;
    (void)locale;
    (void)options;
    return allocate_handle(RIN_HOST_HANDLE_PLURAL, out_handle);
}

int rin_icu_plural_rules_select(rin_icu_client_t* client, rin_icu_handle_t handle, double value, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    if (!get_handle(handle, RIN_HOST_HANDLE_PLURAL))
        return -1;
    if ((value == 1.0) || (value == -1.0))
        return copy_text("one", dest, dest_cap, out_len);
    return copy_text("other", dest, dest_cap, out_len);
}

int rin_icu_plural_rules_destroy(rin_icu_client_t* client, rin_icu_handle_t handle)
{
    (void)client;
    return destroy_handle(handle, RIN_HOST_HANDLE_PLURAL);
}

int rin_icu_display_name(rin_icu_client_t* client, const char* locale, const char* code, uint32_t type, uint32_t style, uint32_t language_display, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    (void)locale;
    (void)type;
    (void)style;
    (void)language_display;
    return copy_text(code, dest, dest_cap, out_len);
}

int rin_icu_list_format(rin_icu_client_t* client, const char* locale, uint32_t type, uint32_t style, const char* const* items, size_t item_count, char* dest, size_t dest_cap, size_t* out_len)
{
    char buffer[4096];
    size_t buffer_len = 0;
    size_t item_index;
    char const* joiner = ", ";
    (void)client;
    (void)locale;
    (void)type;
    (void)style;

    buffer[0] = '\0';
    for (item_index = 0; item_index < item_count; ++item_index) {
        char const* item = (items && items[item_index]) ? items[item_index] : "";
        size_t item_len = c_strlen(item);
        if (item_index > 0 && buffer_len + 2 < sizeof(buffer)) {
            c_memcpy(buffer + buffer_len, joiner, 2);
            buffer_len += 2;
        }
        if (buffer_len + item_len >= sizeof(buffer))
            item_len = sizeof(buffer) - buffer_len - 1;
        c_memcpy(buffer + buffer_len, item, item_len);
        buffer_len += item_len;
        buffer[buffer_len] = '\0';
    }
    return copy_text(buffer, dest, dest_cap, out_len);
}

int rin_icu_relative_time_format(rin_icu_client_t* client, const char* locale, uint32_t style, uint32_t numeric_display, uint32_t unit, double value, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    (void)locale;
    (void)style;
    (void)numeric_display;
    if (value < 0.0)
        return copy_text("1 unit ago", dest, dest_cap, out_len);
    return copy_text(relative_time_unit_name(unit), dest, dest_cap, out_len);
}

int rin_icu_time_zone_current(rin_icu_client_t* client, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    return copy_text("UTC", dest, dest_cap, out_len);
}

int rin_icu_time_zone_canonicalize(rin_icu_client_t* client, const char* time_zone, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    return copy_text(time_zone, dest, dest_cap, out_len);
}

int rin_icu_time_zone_available(rin_icu_client_t* client, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    return copy_text("UTC,Asia/Tokyo,America/New_York,Europe/Berlin", dest, dest_cap, out_len);
}

int rin_icu_time_zone_offset(rin_icu_client_t* client, const char* time_zone, int64_t epoch_ms, int* out_offset_minutes, int* out_in_dst)
{
    (void)client;
    (void)epoch_ms;
    if (!out_offset_minutes || !out_in_dst)
        return -1;
    *out_offset_minutes = time_zone_offset_minutes(time_zone);
    *out_in_dst = 0;
    return 0;
}
