#include "rin_icu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
} rin_host_handle_t;

static rin_host_handle_t g_handles[256];
static rin_icu_handle_t g_next_handle = 1;

static int copy_text(char const* source, char* dest, size_t dest_cap, size_t* out_len)
{
    size_t len;
    if (!dest || dest_cap == 0)
        return -1;
    if (!source)
        source = "";
    len = strlen(source);
    if (len >= dest_cap)
        len = dest_cap - 1;
    memcpy(dest, source, len);
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
    memset(&g_handles[handle], 0, sizeof(g_handles[handle]));
    g_handles[handle].kind = kind;
    *out_handle = handle;
    return 0;
}

static int destroy_handle(rin_icu_handle_t handle, rin_host_handle_kind_t expected_kind)
{
    rin_host_handle_t* state = get_handle(handle, expected_kind);
    if (!state)
        return -1;
    memset(state, 0, sizeof(*state));
    return 0;
}

static int time_zone_offset_minutes(char const* time_zone)
{
    if (!time_zone)
        return 0;
    if (strcmp(time_zone, "Asia/Tokyo") == 0)
        return 9 * 60;
    if (strcmp(time_zone, "America/New_York") == 0)
        return -5 * 60;
    if (strcmp(time_zone, "Europe/Berlin") == 0)
        return 60;
    return 0;
}

int rin_service_start(int scope, const char* id)
{
    (void)scope;
    (void)id;
    return 0;
}

void rin_sleep(unsigned int ms)
{
    usleep(ms * 1000u);
}

int rin_icu_client_open(rin_icu_client_t* out_client)
{
    if (!out_client)
        return -1;
    out_client->fd = 1;
    out_client->next_request_id = 1;
    out_client->reserved[0] = 0;
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

int rin_icu_normalize(rin_icu_client_t* client, char const* form, char const* input, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    (void)form;
    return copy_text(input, dest, dest_cap, out_len);
}

int rin_icu_collator_create(rin_icu_client_t* client, char const* locale, rin_icu_collator_options_t const* options, rin_icu_handle_t* out_handle)
{
    (void)client;
    (void)locale;
    (void)options;
    return allocate_handle(RIN_HOST_HANDLE_COLLATOR, out_handle);
}

int rin_icu_collator_compare(rin_icu_client_t* client, rin_icu_handle_t handle, char const* lhs, char const* rhs, int32_t* out_result)
{
    int result = 0;
    (void)client;
    if (!get_handle(handle, RIN_HOST_HANDLE_COLLATOR) || !out_result)
        return -1;
    if (!lhs)
        lhs = "";
    if (!rhs)
        rhs = "";
    result = strcmp(lhs, rhs);
    *out_result = (result < 0) ? -1 : (result > 0 ? 1 : 0);
    return 0;
}

int rin_icu_collator_destroy(rin_icu_client_t* client, rin_icu_handle_t handle)
{
    (void)client;
    return destroy_handle(handle, RIN_HOST_HANDLE_COLLATOR);
}

int rin_icu_segmenter_create(rin_icu_client_t* client, char const* locale, uint32_t kind, rin_icu_handle_t* out_handle)
{
    (void)client;
    (void)locale;
    (void)kind;
    return allocate_handle(RIN_HOST_HANDLE_SEGMENTER, out_handle);
}

int rin_icu_segmenter_reset(rin_icu_client_t* client, rin_icu_handle_t handle, char const* input, size_t input_len)
{
    rin_host_handle_t* state;
    (void)client;
    (void)input;
    state = get_handle(handle, RIN_HOST_HANDLE_SEGMENTER);
    if (!state)
        return -1;
    state->cursor = 0;
    state->text_len = input_len;
    return 0;
}

int rin_icu_segmenter_next(rin_icu_client_t* client, rin_icu_handle_t handle, int32_t* out_position)
{
    rin_host_handle_t* state;
    (void)client;
    if (!out_position)
        return -1;
    state = get_handle(handle, RIN_HOST_HANDLE_SEGMENTER);
    if (!state)
        return -1;
    if (state->cursor >= state->text_len) {
        *out_position = -1;
        return 0;
    }
    state->cursor++;
    *out_position = (int32_t)state->cursor;
    return 0;
}

int rin_icu_segmenter_destroy(rin_icu_client_t* client, rin_icu_handle_t handle)
{
    (void)client;
    return destroy_handle(handle, RIN_HOST_HANDLE_SEGMENTER);
}

int rin_icu_number_formatter_create(rin_icu_client_t* client, char const* locale, uint32_t style, rin_icu_handle_t* out_handle)
{
    (void)client;
    (void)locale;
    (void)style;
    return allocate_handle(RIN_HOST_HANDLE_NUMBER, out_handle);
}

int rin_icu_number_formatter_format(rin_icu_client_t* client, rin_icu_handle_t handle, char const* value, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    if (!get_handle(handle, RIN_HOST_HANDLE_NUMBER))
        return -1;
    return copy_text(value, dest, dest_cap, out_len);
}

int rin_icu_number_formatter_destroy(rin_icu_client_t* client, rin_icu_handle_t handle)
{
    (void)client;
    return destroy_handle(handle, RIN_HOST_HANDLE_NUMBER);
}

int rin_icu_datetime_formatter_create(rin_icu_client_t* client, char const* locale, uint32_t kind, int32_t tz_offset_minutes, uint32_t hour_cycle, rin_icu_handle_t* out_handle)
{
    (void)client;
    (void)locale;
    (void)kind;
    (void)tz_offset_minutes;
    (void)hour_cycle;
    return allocate_handle(RIN_HOST_HANDLE_DATETIME, out_handle);
}

int rin_icu_datetime_formatter_format_epoch_ms(rin_icu_client_t* client, rin_icu_handle_t handle, double epoch_ms, char* dest, size_t dest_cap, size_t* out_len)
{
    char buffer[64];
    (void)client;
    if (!get_handle(handle, RIN_HOST_HANDLE_DATETIME))
        return -1;
    snprintf(buffer, sizeof(buffer), "%.0f", epoch_ms);
    return copy_text(buffer, dest, dest_cap, out_len);
}

int rin_icu_datetime_formatter_destroy(rin_icu_client_t* client, rin_icu_handle_t handle)
{
    (void)client;
    return destroy_handle(handle, RIN_HOST_HANDLE_DATETIME);
}

int rin_icu_plural_rules_create(rin_icu_client_t* client, char const* locale, uint32_t kind, rin_icu_handle_t* out_handle)
{
    (void)client;
    (void)locale;
    (void)kind;
    return allocate_handle(RIN_HOST_HANDLE_PLURAL, out_handle);
}

int rin_icu_plural_rules_select(rin_icu_client_t* client, rin_icu_handle_t handle, char const* value, char* dest, size_t dest_cap, size_t* out_len)
{
    double numeric_value = 0.0;
    (void)client;
    if (!get_handle(handle, RIN_HOST_HANDLE_PLURAL))
        return -1;
    if (value)
        numeric_value = strtod(value, NULL);
    if (fabs(numeric_value) == 1.0)
        return copy_text("one", dest, dest_cap, out_len);
    return copy_text("other", dest, dest_cap, out_len);
}

int rin_icu_plural_rules_destroy(rin_icu_client_t* client, rin_icu_handle_t handle)
{
    (void)client;
    return destroy_handle(handle, RIN_HOST_HANDLE_PLURAL);
}

int rin_icu_display_name(rin_icu_client_t* client, char const* locale, uint32_t type, char const* code, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    (void)locale;
    (void)type;
    return copy_text(code, dest, dest_cap, out_len);
}

int rin_icu_list_format(rin_icu_client_t* client, char const* locale, uint32_t type, char const* items, size_t items_len, uint32_t item_count, char* dest, size_t dest_cap, size_t* out_len)
{
    char buffer[4096];
    size_t buffer_len = 0;
    size_t item_index;
    size_t cursor = 0;
    char const* joiner = ", ";
    (void)client;
    (void)locale;
    (void)type;
    (void)items_len;

    buffer[0] = '\0';
    for (item_index = 0; item_index < item_count; ++item_index) {
        char const* item = items + cursor;
        size_t item_len = strlen(item);
        if (item_index > 0 && buffer_len + 2 < sizeof(buffer)) {
            memcpy(buffer + buffer_len, joiner, 2);
            buffer_len += 2;
        }
        if (buffer_len + item_len >= sizeof(buffer))
            item_len = sizeof(buffer) - buffer_len - 1;
        memcpy(buffer + buffer_len, item, item_len);
        buffer_len += item_len;
        buffer[buffer_len] = '\0';
        cursor += item_len + 1;
    }
    return copy_text(buffer, dest, dest_cap, out_len);
}

int rin_icu_relative_time_format(rin_icu_client_t* client, char const* locale, char const* unit, char const* value, char* dest, size_t dest_cap, size_t* out_len)
{
    char buffer[256];
    (void)client;
    (void)locale;
    snprintf(buffer, sizeof(buffer), "%s %s", value ? value : "0", unit ? unit : "second");
    return copy_text(buffer, dest, dest_cap, out_len);
}

int rin_icu_time_zone_current(rin_icu_client_t* client, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    return copy_text("UTC", dest, dest_cap, out_len);
}

int rin_icu_time_zone_canonicalize(rin_icu_client_t* client, char const* time_zone, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    return copy_text(time_zone, dest, dest_cap, out_len);
}

int rin_icu_time_zone_available(rin_icu_client_t* client, char* dest, size_t dest_cap, size_t* out_len)
{
    (void)client;
    return copy_text("UTC,Asia/Tokyo,America/New_York,Europe/Berlin", dest, dest_cap, out_len);
}

int rin_icu_time_zone_offset(rin_icu_client_t* client, char const* time_zone, double epoch_ms, int32_t* out_offset_ms, int32_t* out_in_dst)
{
    (void)client;
    (void)epoch_ms;
    if (!out_offset_ms || !out_in_dst)
        return -1;
    *out_offset_ms = time_zone_offset_minutes(time_zone) * 60 * 1000;
    *out_in_dst = 0;
    return 0;
}
